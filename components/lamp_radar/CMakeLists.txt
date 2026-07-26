idf_component_register(
    SRCS "radar.c"
    INCLUDE_DIRS "."
    REQUIRES
        freertos
        log
        esp_driver_uart
)
