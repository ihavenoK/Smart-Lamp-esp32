idf_component_register(
    SRCS "led_strip.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main"
    REQUIRES freertos log led_strip esp_driver_rmt
)
