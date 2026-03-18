#include <stdio.h>
#include "esp_log.h"
#include "pir-sensor.h"
#include "espnowAP.h"

const static char* TAG = "Main";

void app_main(void) {
    ESP_LOGI(TAG, "[step 1] Initializing System...");
    ESP_ERROR_CHECK(wifiInit());

    ESP_LOGI(TAG, "[step 2] Initializing Peripherals...");
    


    ESP_LOGI(TAG, "WiFi 초기화 [1/4]...");
    ESP_LOGI(TAG, "PIR 센서 가동 [2/4]...");
    ESP_LOGI(TAG, "CSI 데이터 전송 준비 완료[3/4]...");
    xTaskCreate(espnow_csi_send, "espnow_csi_send", 4096, NULL, 10, NULL);
    pir_sensor();
    ESP_LOGI(TAG, "습립 모드 및 PIR 가동 [4/4]...");
}