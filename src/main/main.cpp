#include <stdio.h>

#include "cli_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_driver.h"
#include "nvs_flash.h"
#include "zigbee_manager.h"

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

  // Set initial color (Blue for startup)
  led_driver_set_color(0, 0, 20);

  // Initialize Connectivity
  ESP_ERROR_CHECK(zigbee_manager_init());
  ESP_ERROR_CHECK(zigbee_manager_start());

  // Initialize CLI
  ESP_ERROR_CHECK(cli_manager_init());

  while (1) {
    led_driver_toggle();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
