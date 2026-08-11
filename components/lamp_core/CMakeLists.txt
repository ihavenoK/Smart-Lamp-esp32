idf_component_register(
    SRCS "lamp_core.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main" "${CMAKE_CURRENT_LIST_DIR}/../lamp_led" "${CMAKE_CURRENT_LIST_DIR}/../lamp_alarm" "${CMAKE_CURRENT_LIST_DIR}/../lamp_pm"
    REQUIRES freertos log lamp_led lamp_alarm lamp_pm
)
