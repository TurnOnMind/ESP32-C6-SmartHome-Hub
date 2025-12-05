set(SHARED_LINK_PROTO ${CMAKE_CURRENT_LIST_DIR}/../../../shared/include)

idf_component_register(
    SRCS "uart_link.cpp" "wifi_manager.cpp" "bluetooth_manager.cpp"
    INCLUDE_DIRS "include" "${SHARED_LINK_PROTO}"
    PRIV_REQUIRES driver esp_driver_uart esp_timer esp_wifi esp_event nvs_flash bt drivers debug
)
