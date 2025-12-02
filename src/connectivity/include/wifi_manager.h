#pragma once

#include "esp_err.h"

/**
 * @brief Initialize the WiFi Manager.
 *        Sets up the WiFi stack and event loops.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Start WiFi.
 *        Attempts to connect using saved credentials.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Set WiFi credentials.
 *        Saves to NVS and restarts WiFi to connect.
 *
 * @param ssid The WiFi SSID.
 * @param password The WiFi Password.
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t wifi_manager_set_credentials(const char* ssid, const char* password);

/**
 * @brief Start a WiFi scan.
 *        Results will be printed to the log.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t wifi_manager_scan(void);

/**
 * @brief Check if the device is connected to WiFi and has an IP.
 *
 * @return true if connected, false otherwise.
 */
bool wifi_manager_is_connected(void);
