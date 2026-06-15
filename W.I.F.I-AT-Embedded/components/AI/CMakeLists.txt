idf_component_register(
    SRCS "espAI.c"
    INCLUDE_DIRS "./include"
    PRIV_REQUIRES freertos Baseline espnow gpio driver
)