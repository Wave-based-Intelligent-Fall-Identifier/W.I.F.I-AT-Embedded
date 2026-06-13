idf_component_register(
    SRCS "AppMQTT.c"
    PRIV_REQUIRES mqtt nvs_flash esp_netif INCLUDE_DIRS "."
    INCLUDE_DIRS "./include"
)