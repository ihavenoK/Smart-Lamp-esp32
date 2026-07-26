idf_component_register(
    SRCS "sensor.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main" "${CMAKE_CURRENT_LIST_DIR}/../lamp_key" "${CMAKE_CURRENT_LIST_DIR}/../lamp_radar"
    REQUIRES freertos log esp_driver_gpio esp_adc lamp_key lamp_radar
)
