idf_component_register(
    SRCS "wifi.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../main"
    REQUIRES freertos log nvs_flash esp_wifi esp_netif
             esp_http_client esp_https_ota app_update
)
