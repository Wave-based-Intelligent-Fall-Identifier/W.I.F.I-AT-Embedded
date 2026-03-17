#include <stdio.h>
#include "esp_log.h"
#include "pir-sensor.h"
#include "espnowAP.h"

const static char* TAG = "Main";

void app_main(void) {
    ESP_LOGI(TAG, "시스템 시작 [0/3]...");
    ESP_ERROR_CHECK(wifiInit());
    ESP_LOGI(TAG, "WiFi 초기화 [1/3]...");
    ESP_LOGI(TAG, "PIR 센서 가동 [2/3]...");
    pir_sensor();
    ESP_LOGI(TAG, "습립 모드 및 PIR 가동 [3/3]...");
}