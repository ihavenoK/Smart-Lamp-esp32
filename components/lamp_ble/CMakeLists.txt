idf_component_register(
    SRCS "ble_uart.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main"
    REQUIRES freertos log nvs_flash bt
)
