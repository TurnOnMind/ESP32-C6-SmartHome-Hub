# Debugging Tools

This project includes a built-in Command Line Interface (CLI) accessible via the UART console (USB Serial).

## Accessing the CLI

1. Connect the ESP32-C6 to your computer via USB.
2. Open a serial monitor (e.g., `idf.py monitor`, PuTTY, or the VS Code Serial Monitor).
3. Baud rate: 115200 (default).

## Available Commands

### `help`
Lists all available commands and their descriptions.

### `restart`
Performs a software reset of the ESP32-C6.
- **Usage**: `restart`

### `free`
Displays the current heap memory usage.
- **Usage**: `free`
- **Output**:
  - `Internal`: Internal RAM available.
  - `SPIRAM`: External PSRAM available (if equipped).
  - `Min_Free`: Minimum free heap seen since boot (useful for detecting leaks).

### `zb_info`
Prints the current status of the Zigbee stack.
- **Usage**: `zb_info`
- **Output**:
  - Device State (Factory New / Joined).
  - Extended PAN ID.
  - PAN ID.
  - Current Channel.
  - Short Address.

### `log_level`
Sets the global log level. Use this to suppress logs if they interfere with typing.
- **Usage**: `log_level <level>`
- **Levels**: `none`, `error`, `warn`, `info`, `debug`, `verbose`
- **Example**: `log_level none` (Disables all logs)

## Troubleshooting

- **"Command not found"**: Ensure you typed the command correctly. Type `help` to see the list.
- **No Output**: Press `Enter` to see the prompt (`esp32> `).
