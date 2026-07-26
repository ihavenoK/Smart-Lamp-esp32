idf_component_register(
    SRCS "voice.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main"
    REQUIRES freertos log esp_driver_uart
)
