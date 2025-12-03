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

typedef enum {
  LED_ACTIVITY_TX = 0,
  LED_ACTIVITY_RX = 1,
} led_activity_source_t;

/**
 * @brief Set the base "state" color of the LED.
 * The LED will return to this color when not indicating activity.
 *
 * @param r Red intensity (0-255)
 * @param g Green intensity (0-255)
 * @param b Blue intensity (0-255)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_driver_set_state_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Indicate activity (flash the LED).
 * Overrides the state color temporarily.
 *
 * @param source The source of the activity (TX or RX)
 */
void led_driver_mark_activity(led_activity_source_t source);
