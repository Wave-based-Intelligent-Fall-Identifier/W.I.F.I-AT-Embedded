#ifndef CONFIG_H
#define CONFIG_H

typedef struct espnow_payload_t{
    uint8_t command;
} espnow_payload_t;

extern SemaphoreHandle_t nowMutex;

#endif