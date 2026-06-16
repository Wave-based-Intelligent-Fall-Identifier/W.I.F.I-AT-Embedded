#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"  
#include "freertos/semphr.h"

#define id "dd"
#define passwd "ekdus825"

#define BROKER_ADDRESS_URI "mqtt://192.168.0.10:1883"

// 여기 MAC 수정으로 연결
#define AP_MAC_ADDRESS {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

typedef struct espnow_payload_t{
    uint8_t command;
} espnow_payload_t;

extern SemaphoreHandle_t nowMutex;

#endif