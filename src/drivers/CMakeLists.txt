set(SHARED_INCLUDE ${CMAKE_CURRENT_LIST_DIR}/../../../shared/include)

idf_component_register(SRCS "led_driver.cpp"
                       INCLUDE_DIRS "include" "${SHARED_INCLUDE}"
                       REQUIRES debug led_strip esp_timer)
