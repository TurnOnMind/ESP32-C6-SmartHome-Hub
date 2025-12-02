#include "zigbee_manager.h"

#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ZIGBEE_MANAGER";

/* Zigbee configuration */
#define MAX_CHILDREN 10
#define INSTALL_CODE_POLICY_ENABLE false
#define HA_ESP_LIGHT_ENDPOINT 10
#define ESP_ZB_PRIMARY_CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

static void esp_zb_task(void* pvParameters) {
  /* Initialize Zigbee stack */
  esp_zb_cfg_t zb_nwk_cfg = {
      .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
      .install_code_policy = INSTALL_CODE_POLICY_ENABLE,
      .nwk_cfg =
          {
              .zczr_cfg =
                  {
                      .max_children = MAX_CHILDREN,
                  },
          },
  };
  esp_zb_init(&zb_nwk_cfg);

  /* Create a custom Zigbee cluster list */
  esp_zb_attribute_list_t* basic_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
  esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void*)"Espressif");
  esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void*)"Esp32C6_Hub");

  esp_zb_cluster_list_t* cluster_list = esp_zb_zcl_cluster_list_create();
  esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  esp_zb_ep_list_t* ep_list = esp_zb_ep_list_create();
  esp_zb_endpoint_config_t endpoint_config = {.endpoint = HA_ESP_LIGHT_ENDPOINT,
                                              .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
                                              .app_device_id = ESP_ZB_HA_ON_OFF_LIGHT_DEVICE_ID,
                                              .app_device_version = 0};
  esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);

  /* Register the device */
  esp_zb_device_register(ep_list);

  esp_zb_core_action_handler_register(NULL);  // No specific action handler for now

  esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

  ESP_ERROR_CHECK(esp_zb_start(false));

  esp_zb_stack_main_loop();
}

esp_err_t zigbee_manager_init(void) {
  /* Zigbee Platform Configuration */
  esp_zb_platform_config_t config = {
      .radio_config =
          {
              .radio_mode = ZB_RADIO_MODE_NATIVE,
              .radio_uart_config = {},
          },
      .host_config =
          {
              .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
              .host_uart_config = {},
          },
  };
  ESP_ERROR_CHECK(esp_zb_platform_config(&config));

  return ESP_OK;
}

esp_err_t zigbee_manager_start(void) {
  xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
  return ESP_OK;
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask) {
  ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t* signal_struct) {
  uint32_t* p_sg_p = signal_struct->p_app_signal;
  esp_err_t err_status = signal_struct->esp_err_status;
  esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;

  switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
      ESP_LOGI(TAG, "Zigbee stack initialized");
      esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
      break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
      if (err_status == ESP_OK) {
        ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
        if (esp_zb_bdb_is_factory_new()) {
          ESP_LOGI(TAG, "Start network formation");
          esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
        } else {
          ESP_LOGI(TAG, "Device rebooted");
        }
      } else {
        ESP_LOGE(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
      }
      break;
    case ESP_ZB_BDB_SIGNAL_FORMATION:
      if (err_status == ESP_OK) {
        esp_zb_ieee_addr_t extended_pan_id;
        esp_zb_get_extended_pan_id(extended_pan_id);
        ESP_LOGI(TAG,
                 "Formed network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: "
                 "0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                 extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4], extended_pan_id[3],
                 extended_pan_id[2], extended_pan_id[1], extended_pan_id[0], esp_zb_get_pan_id(),
                 esp_zb_get_current_channel(), esp_zb_get_short_address());
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
      } else {
        ESP_LOGI(TAG, "Restart network formation (status: %s)", esp_err_to_name(err_status));
        esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                               ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
      }
      break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
      if (err_status == ESP_OK) {
        ESP_LOGI(TAG, "Network steering started");
      }
      break;
    default:
      ESP_LOGI(TAG, "ZDO signal: 0x%x, status: %s", sig_type, esp_err_to_name(err_status));
      break;
  }
}

void zigbee_manager_print_status(void) {
  if (esp_zb_bdb_is_factory_new()) {
    ESP_LOGI(TAG, "Status: Factory New Device");
  } else {
    esp_zb_ieee_addr_t ext_pan_id;
    esp_zb_get_extended_pan_id(ext_pan_id);
    ESP_LOGI(TAG, "Status: Joined/Formed Network");
    ESP_LOGI(TAG, "Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", ext_pan_id[7], ext_pan_id[6],
             ext_pan_id[5], ext_pan_id[4], ext_pan_id[3], ext_pan_id[2], ext_pan_id[1], ext_pan_id[0]);
    ESP_LOGI(TAG, "PAN ID: 0x%04x", esp_zb_get_pan_id());
    ESP_LOGI(TAG, "Channel: %d", esp_zb_get_current_channel());
    ESP_LOGI(TAG, "Short Address: 0x%04x", esp_zb_get_short_address());
  }
}
