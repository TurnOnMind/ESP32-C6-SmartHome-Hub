#include "include/zigbee_link.h"

#include <cstring>

#define DEBUG_TAG "ZB_LINK"
#include "../debug/include/debug/Debug.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_driver.h"
#include "sdkconfig.h"
#include "zigbee_link_protocol.h"

#if CONFIG_APP_ENABLE_ZB_LINK

namespace {
constexpr size_t kRxBufferSize = 512;
constexpr size_t kTxBufferSize = 512;
constexpr int kHeartbeatIntervalMs = 2000;
constexpr uint32_t kHandshakePollDelayMs = 50;
constexpr int64_t kHandshakeRetryIntervalUs = 750 * 1000;  // retry roughly every 750 ms if needed
constexpr char kLocalHelloMsg[] = "C6 online";

#ifndef CONFIG_APP_ZB_LINK_HANDSHAKE_TIMEOUT_MS
#define CONFIG_APP_ZB_LINK_HANDSHAKE_TIMEOUT_MS 3000
#endif

const char* kTag = DEBUG_TAG;
uart_port_t link_uart() {
  return static_cast<uart_port_t>(CONFIG_APP_ZB_LINK_UART_PORT);
}

struct ParserState {
  uint8_t buffer[ZB_LINK_MAX_PAYLOAD + 8];
  size_t length = 0;
  size_t expected = 0;
};

TaskHandle_t s_rx_task = nullptr;
TaskHandle_t s_hb_task = nullptr;
bool s_initialized = false;
bool s_suspended = false;
ParserState s_parser;
zigbee_link_stats_t s_stats = {};
#ifdef CONFIG_APP_ZB_LINK_DEBUG_LOGS
bool s_debug_frames = true;
#else
bool s_debug_frames = false;
#endif
static uint8_t s_local_secret = 0;

esp_err_t send_frame(zb_link_msg_type_t type, const uint8_t* payload, uint16_t len);

struct HandshakeState {
  bool sent = false;
  bool received = false;
  bool ok = false;
  zb_link_handshake_t remote{};
  int64_t last_sent_us = 0;
};

HandshakeState s_handshake;

const char* frame_type_name(uint8_t type) {
  switch (type) {
    case ZB_LINK_MSG_HELLO:
      return "HELLO";
    case ZB_LINK_MSG_HEARTBEAT:
      return "HEARTBEAT";
    case ZB_LINK_MSG_HANDSHAKE:
      return "HANDSHAKE";
    case ZB_LINK_MSG_ZB_SIGNAL:
      return "ZB_SIGNAL";
    case ZB_LINK_MSG_DEVICE_ANNOUNCE:
      return "DEVICE_ANNOUNCE";
    case ZB_LINK_MSG_ATTR_UPDATE:
      return "ATTR_UPDATE";
    case ZB_LINK_MSG_COMMAND:
      return "COMMAND";
    default:
      return "UNKNOWN";
  }
}

const char* role_to_string(uint8_t role) {
  switch (role) {
    case ZB_LINK_ROLE_HUB:
      return "Hub";
    case ZB_LINK_ROLE_ZIGBEE_COPROC:
      return "Zigbee-CoProc";
    default:
      return "Unknown";
  }
}

void log_handshake_details(const char* label, const zb_link_handshake_t& hs) {
  const bool flow = (hs.flags & ZB_LINK_HANDSHAKE_FLAG_FLOW_CTRL) != 0;
  ESP_LOGI(kTag, "%s: version=%u role=%s (0x%02X) baud=%u flags=0x%02X secret=0x%02X flow_ctrl=%s", label, hs.version,
           role_to_string(hs.role), hs.role, hs.baud_rate, hs.flags, hs.secret, flow ? "on" : "off");
}

uint8_t local_handshake_flags() {
  uint8_t flags = 0;
#ifdef CONFIG_APP_ZB_LINK_USE_HW_FLOWCTRL
  flags |= ZB_LINK_HANDSHAKE_FLAG_FLOW_CTRL;
#endif
  return flags;
}

zb_link_handshake_t build_local_handshake() {
  zb_link_handshake_t payload = {
      .version = ZB_LINK_VERSION,
      .role = ZB_LINK_ROLE_HUB,
      .flags = local_handshake_flags(),
      .secret = s_local_secret,
      .baud_rate = CONFIG_APP_ZB_LINK_UART_BAUDRATE,
  };
  return payload;
}

bool is_local_handshake(const zb_link_handshake_t& candidate) {
  const zb_link_handshake_t local = build_local_handshake();
  return memcmp(&candidate, &local, sizeof(local)) == 0;
}

void log_frame_debug(const char* dir, uint8_t type, uint16_t len) {
  if (!s_debug_frames) {
    return;
  }
  ESP_LOGI(kTag, "[%s] type=%s (0x%02X) len=%u", dir, frame_type_name(type), type, len);
}

void process_handshake(const zb_link_handshake_t& remote) {
  log_handshake_details("Remote handshake", remote);
  if (is_local_handshake(remote)) {
    ESP_LOGW(
        kTag,
        "Ignoring loopback handshake that matches local role/config. Check UART wiring (RX pin is seeing local TX).");
    s_stats.loopback_frames++;
    return;
  }
  s_handshake.received = true;
  s_handshake.remote = remote;
  s_stats.handshake_received = true;
  s_stats.remote_role = remote.role;
  s_stats.remote_flags = remote.flags;
  s_stats.remote_baud = remote.baud_rate;

  bool ok = true;
  if (remote.version != ZB_LINK_VERSION) {
    ok = false;
    ESP_LOGE(kTag, "Handshake mismatch: version %u (expected %u)", remote.version, ZB_LINK_VERSION);
  }
  if (remote.role != ZB_LINK_ROLE_ZIGBEE_COPROC) {
    ok = false;
    ESP_LOGE(kTag, "Handshake mismatch: expected Zigbee co-processor role, got %s", role_to_string(remote.role));
  }
  if (remote.baud_rate != CONFIG_APP_ZB_LINK_UART_BAUDRATE) {
    ok = false;
    ESP_LOGE(kTag, "Handshake mismatch: baud %u (expected %u)", remote.baud_rate, CONFIG_APP_ZB_LINK_UART_BAUDRATE);
  }
  const bool remote_flow = (remote.flags & ZB_LINK_HANDSHAKE_FLAG_FLOW_CTRL) != 0;
#ifdef CONFIG_APP_ZB_LINK_USE_HW_FLOWCTRL
  const bool local_flow = true;
#else
  const bool local_flow = false;
#endif
  if (remote_flow != local_flow) {
    ok = false;
    ESP_LOGE(kTag, "Handshake mismatch: flow control remote=%d local=%d", remote_flow, local_flow);
  }

  s_handshake.ok = ok;
  s_stats.handshake_ok = ok;
  if (ok) {
    ESP_LOGI(kTag, "Handshake OK with %s (baud=%u, flags=0x%02X)", role_to_string(remote.role), remote.baud_rate,
             remote.flags);
  }
}

esp_err_t send_handshake_frame() {
  const zb_link_handshake_t payload = build_local_handshake();
  log_handshake_details("Sending handshake", payload);
  const esp_err_t err = send_frame(ZB_LINK_MSG_HANDSHAKE, reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
  if (err == ESP_OK) {
    s_handshake.sent = true;
    s_handshake.last_sent_us = esp_timer_get_time();
  }
  return err;
}

void reset_parser() {
  s_parser.length = 0;
  s_parser.expected = 0;
}

void handle_frame(const zb_link_frame_t& frame) {
  s_stats.frames_rx++;
  s_stats.last_rx_us = esp_timer_get_time();
  led_driver_mark_activity(LED_ACTIVITY_RX);
  log_frame_debug("RX", frame.type, frame.payload_len);
  switch (frame.type) {
    case ZB_LINK_MSG_HELLO: {
      ESP_LOGI(kTag, "HELLO frame from H2 (%.*s)", frame.payload_len,
               frame.payload_len ? (const char*)frame.payload : "");
      const bool hello_loopback = frame.payload_len == (sizeof(kLocalHelloMsg) - 1) &&
                                  memcmp(frame.payload, kLocalHelloMsg, sizeof(kLocalHelloMsg) - 1) == 0;
      if (hello_loopback) {
        ESP_LOGW(kTag, "Detected HELLO loopback (received own '%s' banner). Verify TX/RX crossover and ground sharing.",
                 kLocalHelloMsg);
        s_stats.loopback_frames++;
      }
      break;
    }
    case ZB_LINK_MSG_HEARTBEAT:
      ESP_LOGD(kTag, "Heartbeat ack (%u bytes)", frame.payload_len);
      break;
    case ZB_LINK_MSG_HANDSHAKE: {
      if (frame.payload_len != sizeof(zb_link_handshake_t)) {
        ESP_LOGW(kTag, "Invalid handshake payload len=%u", frame.payload_len);
        break;
      }
      zb_link_handshake_t remote = {};
      memcpy(&remote, frame.payload, sizeof(remote));
      process_handshake(remote);
      break;
    }
    case ZB_LINK_MSG_ZB_SIGNAL:
      ESP_LOGI(kTag, "Zigbee signal: %.*s", frame.payload_len, (const char*)frame.payload);
      break;
    default:
      ESP_LOGW(kTag, "Unhandled frame type 0x%02X (%u bytes)", frame.type, frame.payload_len);
      break;
  }
}

void push_bytes(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const uint8_t byte = data[i];
    if (s_parser.length == 0 && byte != ZB_LINK_PREAMBLE) {
      continue;
    }
    if (s_parser.length >= sizeof(s_parser.buffer)) {
      reset_parser();
      s_stats.dropped_frames++;
      continue;
    }
    s_parser.buffer[s_parser.length++] = byte;
    if (s_parser.length >= 5 && s_parser.expected == 0) {
      const uint16_t payload_len = (static_cast<uint16_t>(s_parser.buffer[3]) << 8) | s_parser.buffer[4];
      const size_t total = 1 + 1 + 1 + 2 + payload_len + 2;
      if (payload_len > ZB_LINK_MAX_PAYLOAD || total > sizeof(s_parser.buffer)) {
        ESP_LOGW(kTag, "Invalid payload length %u", payload_len);
        reset_parser();
        s_stats.dropped_frames++;
        continue;
      }
      s_parser.expected = total;
    }
    if (s_parser.expected && s_parser.length == s_parser.expected) {
      zb_link_frame_t frame = {};
      if (zb_link_try_parse(s_parser.buffer, s_parser.length, &frame)) {
        handle_frame(frame);
      } else {
        s_stats.crc_errors++;
        ESP_LOGW(kTag, "CRC mismatch or malformed frame");
      }
      reset_parser();
    }
  }
}

esp_err_t send_frame(zb_link_msg_type_t type, const uint8_t* payload, uint16_t len) {
  if (len > ZB_LINK_MAX_PAYLOAD) {
    return ESP_ERR_INVALID_ARG;
  }
  zb_link_frame_t frame{};
  frame.type = static_cast<uint8_t>(type);
  frame.payload_len = len;
  if (len) {
    memcpy(frame.payload, payload, len);
  }
  uint8_t buffer[ZB_LINK_MAX_PAYLOAD + 8] = {};
  const size_t written = zb_link_encode_frame(buffer, sizeof(buffer), &frame);
  if (!written) {
    return ESP_FAIL;
  }
  const int bytes = uart_write_bytes(link_uart(), reinterpret_cast<const char*>(buffer), written);
  if (bytes < 0 || static_cast<size_t>(bytes) != written) {
    return ESP_FAIL;
  }
  uart_wait_tx_done(link_uart(), pdMS_TO_TICKS(20));
  s_stats.frames_tx++;
  s_stats.last_tx_us = esp_timer_get_time();
  led_driver_mark_activity(LED_ACTIVITY_TX);
  log_frame_debug("TX", type, len);
  return ESP_OK;
}

void rx_task(void*) {
  uint8_t* rx_buffer = static_cast<uint8_t*>(heap_caps_malloc(kRxBufferSize, MALLOC_CAP_INTERNAL));
  if (!rx_buffer) {
    ESP_LOGE(kTag, "Failed to allocate RX buffer");
    vTaskDelete(nullptr);
    return;
  }
  while (true) {
    if (s_suspended) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    const int len = uart_read_bytes(link_uart(), rx_buffer, kRxBufferSize, pdMS_TO_TICKS(100));
    if (len > 0) {
      if (s_debug_frames) {
        ESP_LOGI(kTag, "[RX_CHUNK] %d bytes", len);
      }
      push_bytes(rx_buffer, len);
    }
  }
}

void heartbeat_task(void*) {
  const char msg[] = "hb";
  while (true) {
    if (!s_suspended) {
      /*
      // Auto-handshake disabled for manual debugging
      if (!s_handshake.ok) {
        const int64_t now = esp_timer_get_time();
        if (!s_handshake.sent || (now - s_handshake.last_sent_us) >= kHandshakeRetryIntervalUs) {
          ESP_LOGI(kTag, "No valid handshake yet; retrying Zigbee handshake frame");
          send_handshake_frame();
        }
      }
      */
      send_frame(ZB_LINK_MSG_HEARTBEAT, reinterpret_cast<const uint8_t*>(msg), sizeof(msg) - 1);
    }
    vTaskDelay(pdMS_TO_TICKS(kHeartbeatIntervalMs));
  }
}

}  // namespace

esp_err_t zigbee_link_init(void) {
  printf("DEBUG: Inside zigbee_link_init\n");
  DEBUG_FUNC_ENTER();
  if (s_initialized) {
    DEBUG_FUNC_EXIT_RC(ESP_OK);
    return ESP_OK;
  }
  printf("DEBUG: Calling esp_random\n");
  s_local_secret = (uint8_t)(esp_random() & 0xFF);
  uart_config_t uart_config = {};
  uart_config.baud_rate = CONFIG_APP_ZB_LINK_UART_BAUDRATE;
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity = UART_PARITY_DISABLE;
  uart_config.stop_bits = UART_STOP_BITS_1;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.rx_flow_ctrl_thresh = 64;
  uart_config.source_clk = UART_SCLK_DEFAULT;
#ifdef CONFIG_APP_ZB_LINK_USE_HW_FLOWCTRL
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS;
#endif
  printf("DEBUG: Calling uart_param_config\n");
  ESP_ERROR_CHECK(uart_param_config(link_uart(), &uart_config));
#ifdef CONFIG_APP_ZB_LINK_USE_HW_FLOWCTRL
  ESP_ERROR_CHECK(uart_set_pin(link_uart(), CONFIG_APP_ZB_LINK_UART_TX_PIN, CONFIG_APP_ZB_LINK_UART_RX_PIN,
                               CONFIG_APP_ZB_LINK_UART_RTS_PIN, CONFIG_APP_ZB_LINK_UART_CTS_PIN));
#else
  printf("DEBUG: Calling uart_set_pin TX=%d RX=%d\n", CONFIG_APP_ZB_LINK_UART_TX_PIN, CONFIG_APP_ZB_LINK_UART_RX_PIN);
  ESP_ERROR_CHECK(uart_set_pin(link_uart(), CONFIG_APP_ZB_LINK_UART_TX_PIN, CONFIG_APP_ZB_LINK_UART_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
#endif
  printf("DEBUG: Calling uart_driver_install for port %d\n", link_uart());
  ESP_ERROR_CHECK(uart_driver_install(link_uart(), kRxBufferSize, kTxBufferSize, 0, nullptr, 0));

  reset_parser();
  s_stats = {};
  s_stats.initialized = true;
  s_stats.debug_enabled = s_debug_frames;

  BaseType_t created = xTaskCreate(rx_task, "zb_link_rx", 4096, nullptr, 5, &s_rx_task);
  if (created != pdPASS) {
    DEBUG_FUNC_EXIT_RC(ESP_FAIL);
    return ESP_FAIL;
  }
  created = xTaskCreate(heartbeat_task, "zb_link_hb", 2048, nullptr, 4, &s_hb_task);
  if (created != pdPASS) {
    DEBUG_FUNC_EXIT_RC(ESP_FAIL);
    return ESP_FAIL;
  }

  ESP_ERROR_CHECK(
      send_frame(ZB_LINK_MSG_HELLO, reinterpret_cast<const uint8_t*>(kLocalHelloMsg), sizeof(kLocalHelloMsg) - 1));
  ESP_ERROR_CHECK(send_handshake_frame());
  ESP_LOGI(kTag, "UART bridge ready on UART%d (TX=%d RX=%d)", link_uart(), CONFIG_APP_ZB_LINK_UART_TX_PIN,
           CONFIG_APP_ZB_LINK_UART_RX_PIN);
  s_initialized = true;
  DEBUG_FUNC_EXIT_RC(ESP_OK);
  return ESP_OK;
}

void zigbee_link_get_stats(zigbee_link_stats_t* out_stats) {
  DEBUG_FUNC_ENTER();
  DEBUG_PARAM_PTR("out_stats", out_stats);
  if (!out_stats) {
    DEBUG_FUNC_EXIT();
    return;
  }
  *out_stats = s_stats;
  DEBUG_FUNC_EXIT();
}

void zigbee_link_print_status(void) {
  DEBUG_FUNC_ENTER();
  zigbee_link_stats_t stats;
  zigbee_link_get_stats(&stats);
  ESP_LOGI(kTag, "initialized=%d suspended=%d tx=%lu rx=%lu dropped=%lu crc_errors=%lu last_rx=%lldus last_tx=%lldus",
           stats.initialized, stats.suspended, stats.frames_tx, stats.frames_rx, stats.dropped_frames, stats.crc_errors,
           stats.last_rx_us, stats.last_tx_us);
  ESP_LOGI(kTag,
           "debug=%d handshake_received=%d handshake_ok=%d remote_role=0x%02X remote_baud=%u remote_flags=0x%02X "
           "loopbacks=%lu",
           stats.debug_enabled, stats.handshake_received, stats.handshake_ok, stats.remote_role, stats.remote_baud,
           stats.remote_flags, stats.loopback_frames);
  DEBUG_FUNC_EXIT();
}

esp_err_t zigbee_link_send_heartbeat(void) {
  DEBUG_FUNC_ENTER();
  const char hb[] = "manual";
  const esp_err_t result = send_frame(ZB_LINK_MSG_HEARTBEAT, reinterpret_cast<const uint8_t*>(hb), sizeof(hb) - 1);
  DEBUG_FUNC_EXIT_RC(result);
  return result;
}

esp_err_t zigbee_link_send_text(const char* text) {
  DEBUG_FUNC_ENTER();
  DEBUG_PARAM_PTR("text", text);
  if (!text) {
    DEBUG_FUNC_EXIT_RC(ESP_ERR_INVALID_ARG);
    return ESP_ERR_INVALID_ARG;
  }
  const size_t len = strnlen(text, ZB_LINK_MAX_PAYLOAD);
  const esp_err_t result = send_frame(ZB_LINK_MSG_COMMAND, reinterpret_cast<const uint8_t*>(text), len);
  DEBUG_FUNC_EXIT_RC(result);
  return result;
}

esp_err_t zigbee_link_suspend(void) {
  DEBUG_FUNC_ENTER();
  s_suspended = true;
  s_stats.suspended = true;
  DEBUG_FUNC_EXIT();
  return ESP_OK;
}

esp_err_t zigbee_link_resume(void) {
  DEBUG_FUNC_ENTER();
  s_suspended = false;
  s_stats.suspended = false;
  DEBUG_FUNC_EXIT();
  return ESP_OK;
}

esp_err_t zigbee_link_run_startup_check(uint32_t timeout_ms) {
  DEBUG_FUNC_ENTER();
  DEBUG_PARAM_UINT("timeout_ms", timeout_ms);
  if (!s_initialized) {
    DEBUG_FUNC_EXIT_RC(ESP_ERR_INVALID_STATE);
    return ESP_ERR_INVALID_STATE;
  }
  if (timeout_ms == 0) {
    timeout_ms = CONFIG_APP_ZB_LINK_HANDSHAKE_TIMEOUT_MS;
  }
  esp_err_t result = send_handshake_frame();
  if (result != ESP_OK) {
    DEBUG_FUNC_EXIT_RC(result);
    return result;
  }
  const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
  while (true) {
    if (s_handshake.received) {
      result = s_handshake.ok ? ESP_OK : ESP_FAIL;
      DEBUG_FUNC_EXIT_RC(result);
      return result;
    }
    const int64_t now = esp_timer_get_time();
    if (!s_handshake.ok && (now - s_handshake.last_sent_us) >= kHandshakeRetryIntervalUs) {
      ESP_LOGI(kTag, "Startup check: retrying handshake frame");
      send_handshake_frame();
    }
    if (now >= deadline) {
      DEBUG_FUNC_EXIT_RC(ESP_ERR_TIMEOUT);
      return ESP_ERR_TIMEOUT;
    }
    vTaskDelay(pdMS_TO_TICKS(kHandshakePollDelayMs));
  }
}

void zigbee_link_set_debug(bool enable) {
  s_debug_frames = enable;
  s_stats.debug_enabled = enable;
  ESP_LOGI(kTag, "UART debug logging %s", enable ? "enabled" : "disabled");
}

bool zigbee_link_is_debug_enabled(void) {
  return s_debug_frames;
}

bool zigbee_link_handshake_ok(void) {
  return s_handshake.ok;
}

esp_err_t zigbee_link_send_manual_handshake(void) {
  ESP_LOGI(kTag, "Manual handshake requested");
  ESP_LOGI(kTag, "UART Config: TX Pin: %d, RX Pin: %d", CONFIG_APP_ZB_LINK_UART_TX_PIN, CONFIG_APP_ZB_LINK_UART_RX_PIN);
  return send_handshake_frame();
}

#else

esp_err_t zigbee_link_init(void) {
  return ESP_OK;
}

void zigbee_link_get_stats(zigbee_link_stats_t* out_stats) {
  if (out_stats) {
    memset(out_stats, 0, sizeof(*out_stats));
  }
}

void zigbee_link_print_status(void) {
}

esp_err_t zigbee_link_send_heartbeat(void) {
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t zigbee_link_send_text(const char* text) {
  (void)text;
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t zigbee_link_suspend(void) {
  return ESP_OK;
}

esp_err_t zigbee_link_resume(void) {
  return ESP_OK;
}

void zigbee_link_set_debug(bool enable) {
}

bool zigbee_link_is_debug_enabled(void) {
  return false;
}

bool zigbee_link_handshake_ok(void) {
  return false;
}

esp_err_t zigbee_link_send_manual_handshake(void) {
  return ESP_ERR_NOT_SUPPORTED;
}

#endif  // CONFIG_APP_ENABLE_ZB_LINK
