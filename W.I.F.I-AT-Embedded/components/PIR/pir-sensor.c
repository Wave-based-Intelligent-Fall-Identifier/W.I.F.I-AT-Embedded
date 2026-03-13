#include "pir-sensor.h"

const static char *TAG = "Pir-Sensor";
uint8_t RX_MAC_ADDRESS[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void pir_sensor() {
    espnow_payload_t payload;

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "사람 감지, DeepSleep해제");
        ESP_LOGI(TAG, "타이머 리셋");

        payload.command = 1; 
        esp_now_send(RX_MAC_ADDRESS, (uint8_t *)&payload, sizeof(payload));
        vTaskDelay(pdMS_TO_TICKS(100));

        esp_sleep_enable_ext1_wakeup(1ULL << PIR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
        esp_sleep_enable_timer_wakeup(TEN_MINUTES_IN_US);
        
        ESP_LOGI(TAG, "10분 타이머/DeepSleep 시작");
    }

    else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "10분 경과, 사람 없음");

        payload.command = 0; 
        esp_now_send(RX_MAC_ADDRESS, (uint8_t *)&payload, sizeof(payload));
        vTaskDelay(pdMS_TO_TICKS(100));

        esp_sleep_enable_ext1_wakeup(1ULL << PIR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
        
        ESP_LOGI(TAG, "DeepSleep시작");
    }
    
    else {
        ESP_LOGI(TAG, "초기 부팅. PIR 대기 모드로 변경합니다.");
        esp_sleep_enable_ext1_wakeup(1ULL << PIR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    }
    esp_deep_sleep_start();
}