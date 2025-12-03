#include <stdio.h>

#include "bluetooth_manager.h"
#include "cli_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_driver.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "wifi_manager.h"
#include "zigbee_link.h"

static const char* TAG = "MAIN";

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Starting ESP32 Smart Home Central Hub...");
  ESP_LOGI(TAG, "System Init...");

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize Drivers
  ESP_ERROR_CHECK(led_driver_init());
  ESP_ERROR_CHECK(led_driver_set_state_color(0, 0, 20));

  // Initialize Connectivity
  ESP_ERROR_CHECK(wifi_manager_init());
  ESP_ERROR_CHECK(wifi_manager_start());

  // Wait for WiFi to connect (if credentials are present)
  int retry = 0;
  while (!wifi_manager_is_connected() && retry < 20) {
    vTaskDelay(pdMS_TO_TICKS(500));
    retry++;
    if (retry % 2 == 0)
      ESP_LOGI(TAG, "Waiting for WiFi...");
  }

  if (wifi_manager_is_connected()) {
    ESP_LOGI(TAG, "WiFi Connected.");
  } else {
    ESP_LOGW(TAG, "WiFi not connected (no credentials?). Use CLI to provision.");
  }

  ESP_ERROR_CHECK(bluetooth_manager_init());
#if CONFIG_APP_ENABLE_ZB_LINK
  printf("DEBUG: Calling zigbee_link_init\n");
  ESP_ERROR_CHECK(zigbee_link_init());
  printf("DEBUG: zigbee_link_init returned\n");
  esp_err_t hs = zigbee_link_run_startup_check(CONFIG_APP_ZB_LINK_HANDSHAKE_TIMEOUT_MS);
  if (hs == ESP_OK) {
    ESP_LOGI(TAG, "UART handshake with Zigbee co-processor OK");
  } else if (hs == ESP_ERR_TIMEOUT) {
    ESP_LOGW(TAG, "UART handshake timed out; use 'zb_check' to retry");
  } else {
    ESP_LOGE(TAG, "UART handshake failed: %s", esp_err_to_name(hs));
  }
#else
  ESP_LOGI(TAG, "Zigbee UART link disabled via menuconfig.");
#endif

  // Initialize CLI
  ESP_ERROR_CHECK(cli_manager_init());

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
