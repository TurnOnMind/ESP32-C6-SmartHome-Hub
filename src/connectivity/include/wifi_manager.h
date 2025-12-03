#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <esp_err.h>

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
 * @brief Check if WiFi is connected.
 * @return true if connected, false otherwise.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get the current RSSI (Signal Strength).
 * @param rssi Pointer to store the RSSI value.
 * @return esp_err_t ESP_OK on success, ESP_FAIL if not connected.
 */
esp_err_t wifi_manager_get_rssi(int* rssi);

/**
 * @brief Start a WiFi scan.
 *        Results will be printed to the log.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t wifi_manager_scan(void);

#endif  // WIFI_MANAGER_H
