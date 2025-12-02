#include "wifi_manager.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* TAG = "WIFI_MGR";

static bool s_is_connected = false;
static bool s_retry_enabled = true;

/* Signal for WiFi events */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "WiFi Started");
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
    if (s_retry_enabled) {
      ESP_LOGI(TAG, "WiFi Disconnected (Reason: %d). Retrying...", event->reason);
      s_is_connected = false;
      esp_wifi_connect();
    } else {
      ESP_LOGI(TAG, "WiFi Disconnected (Reason: %d). Reconfiguration in progress.", event->reason);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_is_connected = true;
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
    uint16_t number = 0;
    esp_wifi_scan_get_ap_num(&number);
    ESP_LOGI(TAG, "Scan done. Found %d APs.", number);

    if (number > 0) {
      wifi_ap_record_t* ap_info = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * number);
      if (ap_info != NULL) {
        esp_err_t err = esp_wifi_scan_get_ap_records(&number, ap_info);
        if (err == ESP_OK) {
          for (int i = 0; i < number; i++) {
            ESP_LOGI(TAG, "SSID: %-32s | RSSI: %d | Auth: %d", ap_info[i].ssid, ap_info[i].rssi, ap_info[i].authmode);
          }
        } else {
          ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(err));
        }
        free(ap_info);
      } else {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
      }
    }
  }
}

esp_err_t wifi_manager_init(void) {
  // Initialize NVS (needed for WiFi storage)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

  return ESP_OK;
}

esp_err_t wifi_manager_start(void) {
  wifi_config_t wifi_cfg;
  if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK) {
    if (strlen((const char*)wifi_cfg.sta.ssid) > 0) {
      ESP_LOGI(TAG, "Found saved credentials for SSID: %s", wifi_cfg.sta.ssid);
      ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
      ESP_ERROR_CHECK(esp_wifi_start());
      return ESP_OK;
    }
  }

  ESP_LOGI(TAG, "No saved credentials found. Use CLI to set WiFi.");
  return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char* ssid, const char* password) {
  wifi_config_t wifi_config = {};
  strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
  strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
  wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;  // Accept any security mode
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);

  // Stop any ongoing connection attempts
  s_retry_enabled = false;
  esp_wifi_disconnect();
  esp_wifi_stop();

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  s_retry_enabled = true;
  ESP_ERROR_CHECK(esp_wifi_start());
  // esp_wifi_connect() is called by the WIFI_EVENT_STA_START event handler

  return ESP_OK;
}

esp_err_t wifi_manager_scan(void) {
  // Disable retry mechanism to prevent interference with the scan
  s_retry_enabled = false;
  esp_wifi_disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));  // Allow time for disconnection

  wifi_scan_config_t scan_config = {};
  scan_config.show_hidden = true;
  scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan_config.scan_time.active.min = 120;
  scan_config.scan_time.active.max = 150;

  ESP_LOGI(TAG, "Starting WiFi Scan...");
  esp_err_t err = esp_wifi_scan_start(&scan_config, false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(err));
  }
  return err;
}

bool wifi_manager_is_connected(void) {
  return s_is_connected;
}
