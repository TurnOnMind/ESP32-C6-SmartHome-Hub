# Main Controller - ESP32 Smart Home Hub

This is the firmware for the central hub of the Smart Home system, running on an ESP32-C6. It acts as a Zigbee Coordinator.

## Hardware Required

*   **ESP32-C6** Development Board (e.g., ESP32-C6-DevKitC-1)
*   USB Cable for programming and monitoring

## Project Structure

*   `src/main`: Main application entry point.
*   `src/connectivity`: Zigbee stack management.
*   `src/drivers`: Hardware drivers (LEDs, etc.).
*   `partitions.csv`: Custom partition table for Zigbee storage.

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

## License

[MIT](LICENSE)
