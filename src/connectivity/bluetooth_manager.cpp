#include "bluetooth_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

/* NimBLE Stack */
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

static const char* TAG = "BT_MGR";

static int ble_gap_event(struct ble_gap_event* event, void* arg);

static void ble_app_on_sync(void) {
  int rc;
  rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to ensure address (rc=%d)", rc);
    return;
  }
  ESP_LOGI(TAG, "Bluetooth initialized and synced. Address set.");
}

static void host_task(void* param) {
  ESP_LOGI(TAG, "NimBLE Host Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t bluetooth_manager_init(void) {
  ESP_LOGI(TAG, "Initializing Bluetooth (NimBLE)...");

  esp_err_t ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init nimble port");
    return ret;
  }

  ble_hs_cfg.reset_cb = NULL;
  ble_hs_cfg.sync_cb = ble_app_on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  nimble_port_freertos_init(host_task);

  return ESP_OK;
}

static int ble_gap_event(struct ble_gap_event* event, void* arg) {
  struct ble_hs_adv_fields fields;
  int rc;

  switch (event->type) {
    case BLE_GAP_EVENT_DISC:
      rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
      if (rc != 0) {
        return 0;
      }

      /* Print discovered device */
      ESP_LOGI(TAG, "BLE Device: ADDR=%02x:%02x:%02x:%02x:%02x:%02x | RSSI=%d | Name=%.*s", event->disc.addr.val[5],
               event->disc.addr.val[4], event->disc.addr.val[3], event->disc.addr.val[2], event->disc.addr.val[1],
               event->disc.addr.val[0], event->disc.rssi, fields.name_len, fields.name);
      return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
      ESP_LOGI(TAG, "BLE Scan complete");
      return 0;

    default:
      return 0;
  }
}

esp_err_t bluetooth_manager_start_scan(int duration_sec) {
  struct ble_gap_disc_params disc_params;
  int rc;

  // Check if stack is synced
  if (!ble_hs_synced()) {
    ESP_LOGE(TAG, "Bluetooth not synced yet");
    return ESP_FAIL;
  }

  disc_params.filter_duplicates = 1;
  disc_params.passive = 1;
  disc_params.itvl = 0;
  disc_params.window = 0;
  disc_params.filter_policy = 0;
  disc_params.limited = 0;

  ESP_LOGI(TAG, "Starting BLE scan for %d seconds...", duration_sec);
  rc = ble_gap_disc(0, duration_sec * 1000, &disc_params, ble_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to start scan (rc=%d)", rc);
    return ESP_FAIL;
  }
  return ESP_OK;
}
