#pragma once

#include "esp_err.h"

/**
 * @brief Initialize the Zigbee stack as a Coordinator.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t zigbee_manager_init(void);

/**
 * @brief Start the Zigbee stack.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t zigbee_manager_start(void);

/**
 * @brief Print the current Zigbee network status to the log/console.
 */
void zigbee_manager_print_status(void);
