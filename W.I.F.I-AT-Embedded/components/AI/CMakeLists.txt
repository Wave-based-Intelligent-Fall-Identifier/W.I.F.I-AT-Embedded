idf_component_register(
    SRCS "espAI.c" "gru_functions.c"
    INCLUDE_DIRS "./include"
    PRIV_REQUIRES freertos Baseline espnow gpio driver esp_ringbuf
)