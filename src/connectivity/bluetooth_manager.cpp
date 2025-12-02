#include "bluetooth_manager.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

/* NimBLE Stack */
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

static const char* TAG = "BT_MGR";

struct DiscoveredDevice {
  ble_addr_t addr;
  int rssi;
  char name[129];  // Max 128 + null terminator
};

static std::vector<DiscoveredDevice> discovered_devices;

static int ble_gap_event(struct ble_gap_event* event, void* arg);

static void ble_app_on_sync(void) {
  int rc;
  rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to ensure address (rc=%d)", rc);
    return;
  }
  ESP_LOGI(TAG, "Bluetooth initialized and synced. Address set.");
}

static void host_task(void* param) {
  ESP_LOGI(TAG, "NimBLE Host Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t bluetooth_manager_init(void) {
  ESP_LOGI(TAG, "Initializing Bluetooth (NimBLE)...");

  esp_err_t ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init nimble port");
    return ret;
  }

  ble_hs_cfg.reset_cb = NULL;
  ble_hs_cfg.sync_cb = ble_app_on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  nimble_port_freertos_init(host_task);

  return ESP_OK;
}

static int ble_gap_event(struct ble_gap_event* event, void* arg) {
  struct ble_hs_adv_fields adv_fields;
  int rc;

  switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
      const char* name = NULL;
      int name_len = 0;
      const int MAX_NAME_PRINT = 128;
      char name_buf[MAX_NAME_PRINT + 1];
      memset(name_buf, 0, sizeof(name_buf));

      /* Manual AD element scan for Local Name (0x08/0x09)
       * We prefer manual parsing to ensure we handle the length and null-termination correctly.
       */
      if (event->disc.data != NULL && event->disc.length_data > 0) {
        const uint8_t* adv = event->disc.data;
        int adv_len = event->disc.length_data;
        int idx = 0;
        while (idx < adv_len) {
          uint8_t elen = adv[idx];
          if (elen == 0)
            break;  // End of significant data
          if (idx + elen + 1 > adv_len)
            break; /* malformed, stop */

          uint8_t etype = adv[idx + 1];
          if ((etype == 0x08 || etype == 0x09) && elen > 1) { /* Short or Complete Local Name */
            int nlen = elen - 1;
            if (nlen > MAX_NAME_PRINT)
              nlen = MAX_NAME_PRINT;

            memcpy(name_buf, &adv[idx + 2], nlen);
            name_buf[nlen] = '\0';  // Ensure null termination

            name = name_buf;
            name_len = nlen;
            break;  // Found the name, stop parsing
          }
          idx += elen + 1;
        }
      }

      /* Fallback to NimBLE parser if manual failed (e.g. if name is in a different format we missed) */
      if (name == NULL) {
        rc = ble_hs_adv_parse_fields(&adv_fields, event->disc.data, event->disc.length_data);
        if (rc == 0 && adv_fields.name_len > 0 && adv_fields.name != NULL) {
          name = (const char*)adv_fields.name;
          name_len = adv_fields.name_len;
        }
      }

      /* Clamp name length to avoid printing garbage */
      if (name_len > MAX_NAME_PRINT)
        name_len = MAX_NAME_PRINT;

      /* Update or Add to Discovered Devices List */
      bool found = false;
      for (auto& device : discovered_devices) {
        if (memcmp(device.addr.val, event->disc.addr.val, 6) == 0) {
          found = true;
          device.rssi = event->disc.rssi;  // Update RSSI to latest

          // If we found a name and the stored one is empty, update it
          if (name != NULL && name_len > 0 && strlen(device.name) == 0) {
            int copy_len = (name_len > 128) ? 128 : name_len;
            memcpy(device.name, name, copy_len);
            device.name[copy_len] = '\0';
          }
          break;
        }
      }

      if (!found) {
        DiscoveredDevice new_device;
        new_device.addr = event->disc.addr;
        new_device.rssi = event->disc.rssi;
        memset(new_device.name, 0, sizeof(new_device.name));
        if (name != NULL && name_len > 0) {
          int copy_len = (name_len > 128) ? 128 : name_len;
          memcpy(new_device.name, name, copy_len);
          new_device.name[copy_len] = '\0';
        }
        discovered_devices.push_back(new_device);
      }
      return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
      ESP_LOGI(TAG, "BLE Scan complete. Found %d unique devices:", discovered_devices.size());
      ESP_LOGI(TAG, "----------------------------------------------------------------");
      ESP_LOGI(TAG, "%-20s | %-5s | %s", "Address", "RSSI", "Name");
      ESP_LOGI(TAG, "----------------------------------------------------------------");

      for (const auto& device : discovered_devices) {
        const char* display_name = (strlen(device.name) > 0) ? device.name : "(Unknown)";
        ESP_LOGI(TAG, "%02x:%02x:%02x:%02x:%02x:%02x   | %-5d | %s", device.addr.val[5], device.addr.val[4],
                 device.addr.val[3], device.addr.val[2], device.addr.val[1], device.addr.val[0], device.rssi,
                 display_name);
      }
      ESP_LOGI(TAG, "----------------------------------------------------------------");
      return 0;

    default:
      return 0;
  }
}

esp_err_t bluetooth_manager_start_scan(int duration_sec) {
  struct ble_gap_disc_params disc_params;
  int rc;

  // Check if stack is synced
  if (!ble_hs_synced()) {
    ESP_LOGE(TAG, "Bluetooth not synced yet");
    return ESP_FAIL;
  }

  disc_params.filter_duplicates = 0;  // Disable duplicate filtering to see all packets
  disc_params.passive = 0;            // Active scanning
  disc_params.itvl = 0;               // Default interval
  disc_params.window = 0;             // Default window
  disc_params.filter_policy = 0;
  disc_params.limited = 0;

  ESP_LOGI(TAG, "Starting BLE scan for %d seconds...", duration_sec);

  // Clear previous results
  discovered_devices.clear();

  rc = ble_gap_disc(0, duration_sec * 1000, &disc_params, ble_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to start scan (rc=%d)", rc);
    return ESP_FAIL;
  }
  return ESP_OK;
}
