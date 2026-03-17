idf_component_register(
    SRCS "espnowAP.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_wifi log nvs_flash esp_event
)

