#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"  
#include "freertos/semphr.h"

typedef struct espnow_payload_t{
    uint8_t command;
} espnow_payload_t;

extern SemaphoreHandle_t nowMutex;
extern uint8_t retry_count;

#endif