idf_component_register(
    SRCS "key.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main" "${CMAKE_CURRENT_LIST_DIR}/../lamp_core"
    REQUIRES freertos log esp_driver_gpio lamp_core
)
