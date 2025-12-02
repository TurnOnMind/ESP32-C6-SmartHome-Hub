#pragma once

#include "esp_err.h"

/**
 * @brief Initialize the Command Line Interface (CLI).
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t cli_manager_init(void);

/**
 * @brief Start the CLI task.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t cli_manager_start(void);
