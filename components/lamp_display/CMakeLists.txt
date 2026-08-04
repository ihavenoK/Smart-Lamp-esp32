idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main" "${CMAKE_CURRENT_LIST_DIR}/../lamp_alarm"
    REQUIRES freertos log esp_driver_i2c esp_lcd esp_lvgl_port lvgl lamp_alarm
)
