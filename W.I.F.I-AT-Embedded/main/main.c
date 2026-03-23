#include <stdio.h>
#include "esp_log.h"
#include "pir-sensor.h"
#include "espnowAP.h"
#include "gpio_definitions.h"

const static char* TAG = "Main";
SemaphoreHandle_t nowMutex = NULL;

void app_main(void) {
    ESP_LOGI(TAG, "[step 1] Initializing System...");
    ESP_ERROR_CHECK(wifiInit());
    ESP_ERROR_CHECK(esp_now_init());

    ESP_LOGI(TAG, "[step 2] Initializing Peripherals...");
    gpio_pin_init();

    ESP_LOGI(TAG, "[step 3] Creating Mutex...");
    uint8_t retry_count = 0;
    nowMutex = xSemaphoreCreateMutex();
    if (nowMutex) {
        ESP_LOGE(TAG, "Mutex 생성 실패");
        while (retry_count < 3) {
            ESP_LOGW(TAG, "Mutex 재생성 시도 (%d/3)", (retry_count + 1));
        }
    }

    ESP_LOGI(TAG, "[step 4] Initializing ESP-NOW...");
    xTaskCreate(espnow_csi_send, "espnow_csi_send", 4096, NULL, 5, NULL);
    xTaskCreate(pir_sensor, "pir_sensor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "[step 5] Starting system!");
}


// kconfig.projbuild 파일 추가해서 민감한 정보 관리