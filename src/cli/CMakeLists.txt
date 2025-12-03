idf_component_register(
    SRCS "cli_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES console connectivity esp_timer lwip esp_wifi debug
)
