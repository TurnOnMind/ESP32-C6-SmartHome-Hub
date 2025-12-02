#include "led_driver.h"

#include "esp_log.h"
#include "led_strip.h"

static const char* TAG = "LED_DRIVER";

// ESP32-C6-DevKitC-1 usually has the RGB LED on GPIO 8
#define LED_STRIP_BLINK_GPIO 8
#define LED_STRIP_LED_NUMBERS 1
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

static led_strip_handle_t led_strip;
static bool led_state = false;
static uint8_t last_r = 0, last_g = 0, last_b = 0;

esp_err_t led_driver_init(void) {
  ESP_LOGI(TAG, "Initializing LED driver on GPIO %d", LED_STRIP_BLINK_GPIO);

  led_strip_config_t strip_config = {.strip_gpio_num = LED_STRIP_BLINK_GPIO,
                                     .max_leds = LED_STRIP_LED_NUMBERS,
                                     .led_pixel_format = LED_PIXEL_FORMAT_GRB,  // Common for WS2812
                                     .led_model = LED_MODEL_WS2812,
                                     .flags = {
                                         .invert_out = false,
                                     }};

  led_strip_rmt_config_t rmt_config = {.clk_src = RMT_CLK_SRC_DEFAULT,
                                       .resolution_hz = LED_STRIP_RMT_RES_HZ,
                                       .mem_block_symbols = 0,
                                       .flags = {
                                           .with_dma = false,
                                       }};

  esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Install LED strip object failed");
    return ret;
  }

  // Start with LED off
  led_strip_clear(led_strip);
  return ESP_OK;
}

esp_err_t led_driver_set_color(uint8_t r, uint8_t g, uint8_t b) {
  if (!led_strip)
    return ESP_ERR_INVALID_STATE;

  last_r = r;
  last_g = g;
  last_b = b;
  led_state = true;

  ESP_LOGD(TAG, "Setting LED color: R=%d G=%d B=%d", r, g, b);
  return led_strip_set_pixel(led_strip, 0, r, g, b) || led_strip_refresh(led_strip);
}

esp_err_t led_driver_toggle(void) {
  if (!led_strip)
    return ESP_ERR_INVALID_STATE;

  led_state = !led_state;
  if (led_state) {
    return led_strip_set_pixel(led_strip, 0, last_r, last_g, last_b) || led_strip_refresh(led_strip);
  } else {
    return led_strip_clear(led_strip);
  }
}
