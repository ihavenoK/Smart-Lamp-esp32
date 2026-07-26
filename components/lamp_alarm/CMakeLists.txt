idf_component_register(
    SRCS "alarm.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main" "${CMAKE_CURRENT_LIST_DIR}/../lamp_core" "${CMAKE_CURRENT_LIST_DIR}/../lamp_voice"
    REQUIRES freertos log lamp_core lamp_voice
)
