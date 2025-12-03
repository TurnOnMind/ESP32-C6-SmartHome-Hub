#include "cli_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DEBUG_TAG "CLI"
#include "../debug/include/debug/Debug.h"
#include "bluetooth_manager.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"
#include "sdkconfig.h"
#include "wifi_manager.h"
#include "zigbee_link.h"

static const char* TAG = DEBUG_TAG;

/* Auto-pause logging logic */
static bool g_logging_paused = false;
static esp_timer_handle_t g_resume_timer;
static vprintf_like_t g_default_vprintf;

static void resume_logging_timer_cb(void* arg) {
  g_logging_paused = false;
}

static char* custom_hints_cb(const char* buf, int* color, int* bold) {
  // User is typing, pause logging
  g_logging_paused = true;
  // Reset timer (5 seconds)
  esp_timer_stop(g_resume_timer);
  esp_timer_start_once(g_resume_timer, 5000000);
  return NULL;
}

static int custom_vprintf(const char* fmt, va_list args) {
  if (g_logging_paused) {
    return 0;
  }
  if (g_default_vprintf) {
    return g_default_vprintf(fmt, args);
  }
  return vprintf(fmt, args);
}

/* Command Handlers */

static int restart_console(int argc, char** argv) {
  g_logging_paused = false;
  ESP_LOGI(TAG, "Restarting...");
  esp_restart();
  return 0;
}

static int free_mem_console(int argc, char** argv) {
  printf("Free Heap: %lu bytes\n", esp_get_free_heap_size());
  printf("Min Free Heap: %lu bytes\n", esp_get_minimum_free_heap_size());
  return 0;
}

static int zb_info_console(int argc, char** argv) {
  g_logging_paused = false;
  zigbee_link_print_status();
  return 0;
}

static int zb_suspend_console(int argc, char** argv) {
  g_logging_paused = false;
  zigbee_link_suspend();
  printf("Zigbee UART bridge paused\n");
  return 0;
}

static int zb_resume_console(int argc, char** argv) {
  g_logging_paused = false;
  zigbee_link_resume();
  printf("Zigbee UART bridge resumed\n");
  return 0;
}

static int zb_debug_console(int argc, char** argv) {
  g_logging_paused = false;
  if (argc == 1) {
    printf("Zigbee UART debug is %s\n", zigbee_link_is_debug_enabled() ? "ON" : "OFF");
    return 0;
  }
  if (argc != 2) {
    printf("Usage: zb_debug <on|off|status>\n");
    return 1;
  }
  if (strcmp(argv[1], "status") == 0) {
    printf("Zigbee UART debug is %s\n", zigbee_link_is_debug_enabled() ? "ON" : "OFF");
    return 0;
  }
  if (strcmp(argv[1], "on") == 0) {
    zigbee_link_set_debug(true);
    return 0;
  }
  if (strcmp(argv[1], "off") == 0) {
    zigbee_link_set_debug(false);
    return 0;
  }
  printf("Usage: zb_debug <on|off|status>\n");
  return 1;
}

static int zb_handshake_console(int argc, char** argv) {
  g_logging_paused = false;
  zigbee_link_send_manual_handshake();
  return 0;
}

static int zb_check_console(int argc, char** argv) {
  g_logging_paused = false;
  uint32_t timeout_ms = CONFIG_APP_ZB_LINK_HANDSHAKE_TIMEOUT_MS;
  if (argc == 2) {
    timeout_ms = (uint32_t)atoi(argv[1]);
  } else if (argc > 2) {
    printf("Usage: zb_check [timeout_ms]\n");
    return 1;
  }
  esp_err_t err = zigbee_link_run_startup_check(timeout_ms);
  if (err == ESP_OK) {
    printf("Handshake OK (remote role confirmed)\n");
    return 0;
  }
  if (err == ESP_ERR_TIMEOUT) {
    printf("Handshake timed out after %" PRIu32 " ms\n", timeout_ms);
  } else {
    printf("Handshake failed: %s\n", esp_err_to_name(err));
  }
  return err == ESP_OK ? 0 : 1;
}

static int log_level_console(int argc, char** argv) {
  if (argc != 2) {
    printf("Usage: log_level <none|error|warn|info|debug|verbose>\n");
    return 1;
  }

  esp_log_level_t level = ESP_LOG_NONE;
  if (strcmp(argv[1], "none") == 0) {
    level = ESP_LOG_NONE;
  } else if (strcmp(argv[1], "error") == 0) {
    level = ESP_LOG_ERROR;
  } else if (strcmp(argv[1], "warn") == 0) {
    level = ESP_LOG_WARN;
  } else if (strcmp(argv[1], "info") == 0) {
    level = ESP_LOG_INFO;
  } else if (strcmp(argv[1], "debug") == 0) {
    level = ESP_LOG_DEBUG;
  } else if (strcmp(argv[1], "verbose") == 0) {
    level = ESP_LOG_VERBOSE;
  } else {
    printf("Invalid log level. Use: none, error, warn, info, debug, verbose\n");
    return 1;
  }

  esp_log_level_set("*", level);
  printf("Log level set to %s\n", argv[1]);
  return 0;
}

static int wifi_set_console(int argc, char** argv) {
  if (argc != 3) {
    printf("Usage: wifi_set <ssid> <password>\n");
    return 1;
  }
  wifi_manager_set_credentials(argv[1], argv[2]);
  return 0;
}

static int wifi_scan_console(int argc, char** argv) {
  wifi_manager_scan();
  return 0;
}

static int ble_scan_console(int argc, char** argv) {
  int duration = 5;
  if (argc == 2) {
    duration = atoi(argv[1]);
  }
  bluetooth_manager_start_scan(duration);
  return 0;
}

static int wifi_ps_console(int argc, char** argv) {
  if (argc != 2) {
    printf("Usage: wifi_ps <none|min|max>\n");
    return 1;
  }

  esp_err_t err = ESP_OK;
  if (strcmp(argv[1], "none") == 0) {
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    printf("WiFi Power Save set to NONE\n");
  } else if (strcmp(argv[1], "min") == 0) {
    err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    printf("WiFi Power Save set to MIN_MODEM\n");
  } else if (strcmp(argv[1], "max") == 0) {
    err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    printf("WiFi Power Save set to MAX_MODEM\n");
  } else {
    printf("Invalid mode. Use: none, min, max\n");
    return 1;
  }

  if (err != ESP_OK) {
    printf("Failed to set PS mode: %s\n", esp_err_to_name(err));
    return 1;
  }
  return 0;
}

/* Ping Command */

static void cmd_ping_on_ping_end(esp_ping_handle_t hdl, void* args) {
  uint32_t transmitted;
  uint32_t received;
  uint32_t total_time_ms;
  esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
  esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
  esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
  printf("%d packets transmitted, %d received, time %dms\n", (int)transmitted, (int)received, (int)total_time_ms);
  esp_ping_delete_session(hdl);
}

static void cmd_ping_on_ping_success(esp_ping_handle_t hdl, void* args) {
  uint8_t ttl;
  uint16_t seqno;
  uint32_t elapsed_time, recv_len;
  ip_addr_t target_addr;
  esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
  esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
  esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
  esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
  printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%d ms\n", (int)recv_len, ipaddr_ntoa((ip_addr_t*)&target_addr),
         (int)seqno, (int)ttl, (int)elapsed_time);
}

static void cmd_ping_on_ping_timeout(esp_ping_handle_t hdl, void* args) {
  uint16_t seqno;
  ip_addr_t target_addr;
  esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
  printf("From %s icmp_seq=%d timeout\n", ipaddr_ntoa((ip_addr_t*)&target_addr), (int)seqno);
}

static int ping_console(int argc, char** argv) {
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();

  if (argc < 2) {
    printf("Usage: ping <ip_address>\n");
    return 1;
  }

  ip_addr_t target_addr;
  struct addrinfo hint;
  struct addrinfo* res = NULL;
  memset(&hint, 0, sizeof(hint));
  memset(&target_addr, 0, sizeof(target_addr));

  // Resolve hostname/IP
  if (getaddrinfo(argv[1], NULL, &hint, &res) != 0) {
    printf("ping: unknown host %s\n", argv[1]);
    return 1;
  }

  if (res->ai_family == AF_INET) {
    struct in_addr addr4 = ((struct sockaddr_in*)(res->ai_addr))->sin_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
  } else {
    printf("ping: only IPv4 supported for now\n");
    freeaddrinfo(res);
    return 1;
  }
  freeaddrinfo(res);

  config.target_addr = target_addr;
  config.count = 5;  // Default to 5 packets

  esp_ping_callbacks_t cbs = {};
  cbs.on_ping_success = cmd_ping_on_ping_success;
  cbs.on_ping_timeout = cmd_ping_on_ping_timeout;
  cbs.on_ping_end = cmd_ping_on_ping_end;
  cbs.cb_args = NULL;

  esp_ping_handle_t ping;
  esp_ping_new_session(&config, &cbs, &ping);
  esp_ping_start(ping);

  return 0;
}

static int wifi_test_console(int argc, char** argv) {
  printf("Running WiFi Self-Test...\n");

  bool connected = wifi_manager_is_connected();
  printf("1. Connection Status: %s\n", connected ? "CONNECTED" : "DISCONNECTED");

  if (connected) {
    int rssi = 0;
    if (wifi_manager_get_rssi(&rssi) == ESP_OK) {
      printf("2. Signal Strength (RSSI): %d dBm\n", rssi);
    } else {
      printf("2. Signal Strength: Failed to read\n");
    }

    // Ping Gateway (usually 192.168.0.1 or similar, but we can just ping 8.8.8.8 for internet check)
    printf("3. Internet Connectivity Test (Ping 8.8.8.8)...\n");
    char* ping_args[] = {(char*)"ping", (char*)"8.8.8.8"};
    ping_console(2, ping_args);
  } else {
    printf("Skipping connectivity tests (WiFi not connected)\n");
  }

  return 0;
}

/* Initialization */

esp_err_t cli_manager_init(void) {
  esp_console_repl_t* repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  /* Prompt to be printed before each line.
   * This can be customized, made dynamic, etc.
   */
  repl_config.prompt = "esp32-hub> ";
  repl_config.max_cmdline_length = 1024;

  /* Register commands */
  esp_console_register_help_command();

  const esp_console_cmd_t restart_cmd = {
      .command = "restart",
      .help = "Restart the device",
      .hint = NULL,
      .func = &restart_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&restart_cmd));

  const esp_console_cmd_t free_cmd = {
      .command = "free",
      .help = "Get the current size of free heap memory",
      .hint = NULL,
      .func = &free_mem_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&free_cmd));

  const esp_console_cmd_t zb_info_cmd = {
      .command = "zb_info",
      .help = "Print Zigbee link status",
      .hint = NULL,
      .func = &zb_info_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&zb_info_cmd));

  const esp_console_cmd_t zb_suspend_cmd = {
      .command = "zb_suspend",
      .help = "Pause the Zigbee UART bridge",
      .hint = NULL,
      .func = &zb_suspend_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&zb_suspend_cmd));

  const esp_console_cmd_t zb_resume_cmd = {
      .command = "zb_resume",
      .help = "Resume the Zigbee UART bridge",
      .hint = NULL,
      .func = &zb_resume_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&zb_resume_cmd));

  const esp_console_cmd_t zb_debug_cmd = {
      .command = "zb_debug",
      .help = "Toggle Zigbee UART debug logs: zb_debug <on|off|status>",
      .hint = NULL,
      .func = &zb_debug_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&zb_debug_cmd));

  const esp_console_cmd_t zb_handshake_cmd = {
      .command = "zb_handshake",
      .help = "Send a manual Zigbee handshake frame",
      .hint = NULL,
      .func = &zb_handshake_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&zb_handshake_cmd));

  const esp_console_cmd_t zb_check_cmd = {
      .command = "zb_check",
      .help = "Run UART handshake check: zb_check [timeout_ms]",
      .hint = NULL,
      .func = &zb_check_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&zb_check_cmd));

  const esp_console_cmd_t log_level_cmd = {
      .command = "log_level",
      .help = "Set the log level (none, error, warn, info, debug, verbose)",
      .hint = NULL,
      .func = &log_level_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&log_level_cmd));

  const esp_console_cmd_t ble_scan_cmd = {
      .command = "ble_scan",
      .help = "Scan for BLE devices: ble_scan [duration_sec]",
      .hint = NULL,
      .func = &ble_scan_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&ble_scan_cmd));

  const esp_console_cmd_t wifi_set_cmd = {
      .command = "wifi_set",
      .help = "Set WiFi credentials: wifi_set <ssid> <password>",
      .hint = NULL,
      .func = &wifi_set_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_set_cmd));

  const esp_console_cmd_t wifi_scan_cmd = {
      .command = "wifi_scan",
      .help = "Scan for available WiFi networks",
      .hint = NULL,
      .func = &wifi_scan_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_scan_cmd));

  const esp_console_cmd_t wifi_ps_cmd = {
      .command = "wifi_ps",
      .help = "Set WiFi Power Save mode: wifi_ps <none|min|max>",
      .hint = NULL,
      .func = &wifi_ps_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_ps_cmd));

  const esp_console_cmd_t ping_cmd = {
      .command = "ping",
      .help = "Ping a host: ping <ip_address>",
      .hint = NULL,
      .func = &ping_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&ping_cmd));

  const esp_console_cmd_t wifi_test_cmd = {
      .command = "wifi_test",
      .help = "Run WiFi self-test (Status, RSSI, Ping)",
      .hint = NULL,
      .func = &wifi_test_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_test_cmd));

  /* Install console REPL */
  esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

  // Setup auto-pause logging
  const esp_timer_create_args_t timer_args = {
      .callback = &resume_logging_timer_cb,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "resume_log",
      .skip_unhandled_events = false,
  };
  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_resume_timer));

  g_default_vprintf = esp_log_set_vprintf(custom_vprintf);
  linenoiseSetHintsCallback(custom_hints_cb);

  ESP_ERROR_CHECK(esp_console_start_repl(repl));

  return ESP_OK;
}

esp_err_t cli_manager_start(void) {
  // REPL is started in init for simplicity with the helper function
  return ESP_OK;
}
