idf_component_register(
    SRCS "AppMQTTSend.c" "AppMQTT.c"
    INCLUDE_DIRS "./include"
    REQUIRES mqtt esp_timer nvs_flash
    PRIV_REQUIRES esp_netif APcommon espnow
)