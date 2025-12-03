#ifndef ZIGBEE_LINK_H_
#define ZIGBEE_LINK_H_

#include <stdbool.h>
#include <stdint.h>

#if __has_include("esp_err.h")
#include "esp_err.h"
#else
typedef int esp_err_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool initialized;
  bool suspended;
  bool debug_enabled;
  bool handshake_received;
  bool handshake_ok;
  uint8_t remote_role;
  uint8_t remote_flags;
  uint32_t remote_baud;
  uint32_t frames_rx;
  uint32_t frames_tx;
  uint32_t crc_errors;
  uint32_t dropped_frames;
  uint32_t loopback_frames;
  int64_t last_rx_us;
  int64_t last_tx_us;
} zigbee_link_stats_t;

esp_err_t zigbee_link_init(void);
esp_err_t zigbee_link_run_startup_check(uint32_t timeout_ms);
void zigbee_link_get_stats(zigbee_link_stats_t* out_stats);
void zigbee_link_print_status(void);
esp_err_t zigbee_link_send_heartbeat(void);
esp_err_t zigbee_link_send_text(const char* text);
esp_err_t zigbee_link_suspend(void);
esp_err_t zigbee_link_resume(void);
void zigbee_link_set_debug(bool enable);
bool zigbee_link_is_debug_enabled(void);
bool zigbee_link_handshake_ok(void);
esp_err_t zigbee_link_send_manual_handshake(void);

#ifdef __cplusplus
}
#endif

#endif  // ZIGBEE_LINK_H_
