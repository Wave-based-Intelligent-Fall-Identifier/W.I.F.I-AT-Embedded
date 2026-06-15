#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"  
#include "freertos/semphr.h"

#define id "wi_dje21"
#define passwd "Djedsmhspw20215!"

#define BROKER_ADDRESS_URI "mqtt://192.168.0.10:1883"

// 여기 MAC 수정으로 연결
#define AP_MAC_ADDRESS {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

typedef struct espnow_payload_t{
    uint8_t command;
} espnow_payload_t;

extern SemaphoreHandle_t nowMutex;
extern uint8_t retry_count;

#endif