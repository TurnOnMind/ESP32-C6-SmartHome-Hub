#pragma once

#include "esp_err.h"

/**
 * @brief Initialize the onboard RGB LED
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_driver_init(void);

/**
 * @brief Set the LED color
 *
 * @param r Red intensity (0-255)
 * @param g Green intensity (0-255)
 * @param b Blue intensity (0-255)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_driver_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Toggle the LED (between off and last set color)
 *
 * @return esp_err_t
 */
esp_err_t led_driver_toggle(void);
