#include "include/led_driver.h"

#define DEBUG_TAG "LED_DRIVER"
#include "../debug/include/debug/Debug.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

static const char* TAG = DEBUG_TAG;

// ESP32-C6-DevKitC-1 usually has the RGB LED on GPIO 8
#define LED_STRIP_BLINK_GPIO 8
#define LED_STRIP_LED_NUMBERS 1
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

static led_strip_handle_t led_strip;
static bool led_state = false;
static uint8_t last_r = 0, last_g = 0, last_b = 0;

// Activity LED state
static esp_timer_handle_t s_pulse_timer = nullptr;
static bool s_activity_showing = false;
static uint8_t s_idle_r = 0;
static uint8_t s_idle_g = 0;
static uint8_t s_idle_b = 0;

constexpr uint8_t kActivityColorR = 60;
constexpr uint8_t kActivityColorG = 40;
constexpr uint8_t kActivityColorB = 0;
constexpr uint64_t kPulseDurationUs = 150000;  // 150 ms

static void pulse_timer_callback(void* arg) {
  s_activity_showing = false;
  led_driver_set_color(s_idle_r, s_idle_g, s_idle_b);
}

esp_err_t led_driver_init(void) {
  DEBUG_FUNC_ENTER();
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
    DEBUG_FUNC_EXIT_RC(ret);
    return ret;
  }

  // Create pulse timer
  esp_timer_create_args_t timer_args = {
      .callback = &pulse_timer_callback,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "led_activity",
      .skip_unhandled_events = false,
  };
  ret = esp_timer_create(&timer_args, &s_pulse_timer);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create LED pulse timer");
    DEBUG_FUNC_EXIT_RC(ret);
    return ret;
  }

  // Start with LED off
  led_strip_clear(led_strip);
  DEBUG_FUNC_EXIT();
  return ESP_OK;
}

esp_err_t led_driver_set_color(uint8_t r, uint8_t g, uint8_t b) {
  DEBUG_FUNC_ENTER();
  DEBUG_PARAM_UINT("r", r);
  DEBUG_PARAM_UINT("g", g);
  DEBUG_PARAM_UINT("b", b);
  if (!led_strip) {
    DEBUG_FUNC_EXIT_RC(ESP_ERR_INVALID_STATE);
    return ESP_ERR_INVALID_STATE;
  }

  last_r = r;
  last_g = g;
  last_b = b;
  led_state = true;

  // ESP_LOGD(TAG, "Setting LED color: R=%d G=%d B=%d", r, g, b);
  esp_err_t err = led_strip_set_pixel(led_strip, 0, r, g, b);
  if (err != ESP_OK) {
    DEBUG_FUNC_EXIT_RC(err);
    return err;
  }
  err = led_strip_refresh(led_strip);
  DEBUG_FUNC_EXIT_RC(err);
  return err;
}

esp_err_t led_driver_set_state_color(uint8_t r, uint8_t g, uint8_t b) {
  DEBUG_FUNC_ENTER();
  s_idle_r = r;
  s_idle_g = g;
  s_idle_b = b;
  if (!s_activity_showing) {
    return led_driver_set_color(r, g, b);
  }
  DEBUG_FUNC_EXIT();
  return ESP_OK;
}

void led_driver_mark_activity(led_activity_source_t source) {
  if (!led_strip)
    return;

  s_activity_showing = true;
  led_driver_set_color(kActivityColorR, kActivityColorG, kActivityColorB);

  if (s_pulse_timer) {
    esp_timer_stop(s_pulse_timer);
    esp_timer_start_once(s_pulse_timer, kPulseDurationUs);
  }
}

esp_err_t led_driver_toggle(void) {
  DEBUG_FUNC_ENTER();
  if (!led_strip) {
    DEBUG_FUNC_EXIT_RC(ESP_ERR_INVALID_STATE);
    return ESP_ERR_INVALID_STATE;
  }

  led_state = !led_state;
  if (led_state) {
    esp_err_t err = led_strip_set_pixel(led_strip, 0, last_r, last_g, last_b);
    if (err != ESP_OK) {
      DEBUG_FUNC_EXIT_RC(err);
      return err;
    }
    const esp_err_t refresh = led_strip_refresh(led_strip);
    DEBUG_FUNC_EXIT_RC(refresh);
    return refresh;
  } else {
    const esp_err_t clear = led_strip_clear(led_strip);
    DEBUG_FUNC_EXIT_RC(clear);
    return clear;
  }
}
