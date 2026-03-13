#include <stdio.h>
#include "esp_log.h"
#include "pir-sensor.h"

const static char* TAG = "Main";

void app_main(void) {
    ESP_LOGI(TAG, "시스템 시작 [0/5]...");
    pir_sensor();
    ESP_LOGI(TAG, "습립 모드 및 PIR 가동 [1/5]...");
}

// PIR       . o
// Wifi AP   .
// ESP-NOW   . o
// sleep     . o