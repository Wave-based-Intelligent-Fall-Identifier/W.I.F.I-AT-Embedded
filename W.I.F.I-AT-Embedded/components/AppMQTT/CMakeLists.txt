idf_component_register(
    SRCS "AppMQTTSend.c" "AppMQTT.c"
    INCLUDE_DIRS "./include"
    REQUIRES mqtt esp_timer nvs_flash esp_netif esp_event esp_wifi APcommon
)