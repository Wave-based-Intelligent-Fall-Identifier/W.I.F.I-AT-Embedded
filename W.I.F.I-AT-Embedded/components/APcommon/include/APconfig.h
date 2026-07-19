#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define id                 CONFIG_ESP_WIFI_SSID
#define passwd             CONFIG_ESP_WIFI_PASSWORD

#define BROKER_ADDRESS_URI CONFIG_ESP_BROKER_URI

// 테스트 모드: 정의하면 딥슬립 없이 항시 동작 (CSI/baseline 디버깅용)
// 배포 시에는 반드시 주석 처리할 것
#define TEST

// 여기 MAC 수정으로 연결
#define AP_MAC_ADDRESS {0x78, 0x1C, 0x3C, 0xF4, 0xAF, 0xF4}

typedef struct espnow_payload_t{
    uint8_t command;
} espnow_payload_t;

extern SemaphoreHandle_t nowMutex;

#endif