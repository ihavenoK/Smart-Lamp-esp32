idf_component_register(
    SRCS "lamp_pm.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main" "${CMAKE_CURRENT_LIST_DIR}/../lamp_led" "${CMAKE_CURRENT_LIST_DIR}/../lamp_display"
    REQUIRES freertos log esp_hw_support lamp_led lamp_display
)
