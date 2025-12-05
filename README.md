# Main Controller - ESP32 Smart Home Hub

This is the WiFi/BLE + automation brain for the Smart Home system, running on an ESP32-C6. All Zigbee radio duties now live on the companion ESP32-H2 and the two MCUs communicate over a dedicated UART link.

## Hardware Required

*   **ESP32-C6** Development Board (e.g., ESP32-C6-DevKitC-1)
*   USB Cable for programming and monitoring

## Project Structure

*   `src/main`: Main application entry point.
*   `src/cli`: UART Command Line Interface (Debugging).
*   `src/connectivity`: WiFi/BLE managers plus the `uart_link` UART bridge to the ESP32-H2.
*   `src/drivers`: Hardware drivers (LEDs, etc.).
*   `partitions.csv`: Custom partition table that keeps OTA slots plus a `zb_proxy` partition for mirrored Zigbee metadata received from the H2.

### UART wiring to ESP32-H2

| Signal              | ESP32-C6 pin  | ESP32-H2 pin  | Notes                                           |
| ------------------- | ------------- | ------------- | ----------------------------------------------- |
| UART TX (C6 → H2)   | GPIO4 (U1TXD) | GPIO6 (U1RXD) | Command/control frames                          |
| UART RX (H2 → C6)   | GPIO5 (U1RXD) | GPIO7 (U1TXD) | Attribute updates + events                      |
| UART RTS (optional) | GPIO12        | GPIO10        | Enable via `CONFIG_APP_UART_LINK_USE_HW_FLOWCTRL` |
| UART CTS (optional) | GPIO13        | GPIO11        | Enable via `CONFIG_APP_UART_LINK_USE_HW_FLOWCTRL` |

All link settings (baud rate, pins, flow control) can be tweaked under `menuconfig → Application Configuration`.

## Debugging

This firmware includes a built-in CLI for debugging.
The CLI exposes helper commands (`zb_info`, `zb_suspend`, `zb_resume`) that now report and control the UART bridge instead of a local Zigbee stack. See [docs/DEBUGGING.md](docs/DEBUGGING.md) for the full list (`restart`, `free`, `zb_info`, `log_level`, etc.).

## How to Build and Flash

Prerequisites: [ESP-IDF v5.1+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/index.html)

1.  **Set up the environment**:
    ```bash
    . $HOME/esp/esp-idf/export.sh
    ```

2.  **Build the project**:
    ```bash
    idf.py build
    ```

3.  **Flash to the device**:
    ```bash
    idf.py -p /dev/ttyUSB0 flash
    ```
    *(Replace `/dev/ttyUSB0` with your device's port)*

4.  **Monitor the output**:
    ```bash
    idf.py -p /dev/ttyUSB0 monitor
    ```

### Auto-detect & flash both boards

When both devkits are plugged into USB, you can let the toolchain figure out
which serial port belongs to each board and flash them sequentially:

```bash
python scripts/auto_flash.py --action build-flash
```

The helper scans all `/dev/ttyUSB*` ports with `esptool`, maps the ESP32-C6 hub
and ESP32-H2 tester automatically, and reuses `scripts/run_c6.sh` /
`scripts/run_h2.sh` under the hood. Pass `--action all` if you also want each
monitor session to open after flashing, or `--dry-run` to only print the
detected mapping.

## License

[MIT](LICENSE)
