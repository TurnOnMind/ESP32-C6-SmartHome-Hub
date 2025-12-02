#include "cli_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"
#include "zigbee_manager.h"

static const char* TAG = "CLI";

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
  zigbee_manager_print_status();
  return 0;
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
      .help = "Print Zigbee network status",
      .hint = NULL,
      .func = &zb_info_console,
      .argtable = NULL,
      .func_w_context = NULL,
      .context = NULL,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&zb_info_cmd));

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
