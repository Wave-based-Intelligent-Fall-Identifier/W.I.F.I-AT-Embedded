#include <stdio.h>
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "pir-sensor.h"
#include "espnowAP.h"
#include "gpio_definitions.h"
#include "originFunc.h"
#include "espAI.h"
#include "baseline.h"
#include "common_struct.h"

const static char* TAG = "Main";
SemaphoreHandle_t nowMutex = NULL;

void app_main(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "MAC Address: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "[step 1] Creating Mutex...");
    uint8_t retry_count = 0;
    nowMutex = xSemaphoreCreateMutex();
    if (nowMutex == NULL) {
        ESP_LOGE(TAG, "Mutex 생성 실패");
        while (retry_count < 3 && nowMutex == NULL) {
            ESP_LOGW(TAG, "Mutex 재생성 시도 (%d/3)", (retry_count + 1));
            vTaskDelay(pdMS_TO_TICKS(1000));
            nowMutex = xSemaphoreCreateMutex();
            retry_count++;
        }
        if (nowMutex == NULL) {
            ESP_LOGE(TAG, "Mutex 생성 최종 실패. 시스템을 재시작합니다.");
            esp_restart();
        }
    }

    ESP_LOGI(TAG, "[step 2] Initializing WiFi (STA)...");
    ESP_ERROR_CHECK(wifiInit());

    ESP_LOGI(TAG, "[step 3] Initializing ESP-NOW...");
    ESP_ERROR_CHECK(espnowInit());

    ESP_LOGI(TAG, "[step 4] Initializing Peripherals...");
    gpio_pin_init(); 

    ESP_LOGI(TAG, "[step 5] Starting MQTT...");
    mqtt5_init(NULL);

    ESP_LOGI(TAG, "[step 6] Initializing baseline & CSI receive...");
    esp_err_t err = baseline_init(&g_baseline);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "baseline 초기화 성공");
    } else {
        ESP_LOGE(TAG, "baseline 초기화 실패");
    }
    ESP_ERROR_CHECK(csi_recv_init());
    

    // [step 7] 태스크 생성
    ESP_LOGI(TAG, "[step 7] Starting tasks!");
    xTaskCreate(pir_sensor, "pir_sensor", 4096, NULL, 5, NULL);
    xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 5, NULL);
    xTaskCreate(esp_ai_task, "esp_ai_task", 6144, NULL, 5, NULL);
}