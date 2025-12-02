#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Bluetooth stack (NimBLE)
 * @return ESP_OK on success
 */
esp_err_t bluetooth_manager_init(void);

/**
 * @brief Start a BLE scan
 * @param duration_sec Duration in seconds
 * @return ESP_OK on success
 */
esp_err_t bluetooth_manager_start_scan(int duration_sec);

#ifdef __cplusplus
}
#endif
