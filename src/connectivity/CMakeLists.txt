idf_component_register(
    SRCS "zigbee_manager.cpp" "wifi_manager.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__esp-zigbee-lib espressif__esp-zboss-lib esp_wifi esp_event nvs_flash
)
