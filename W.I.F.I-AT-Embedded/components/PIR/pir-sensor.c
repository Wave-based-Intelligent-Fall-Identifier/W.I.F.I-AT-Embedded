#include "pir-sensor.h"

const static char *TAG = "Pir-Sensor";
const static uint8_t RX_MAC_ADDRESS[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void pir_sensor(void* pvParameters) {
    espnow_payload_t payload = {0};
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // PIR이 사람을 감지함, 10분 타이머 리셋 
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "사람 감지, 10분간 CSI 전송 모드 유지");

        payload.command = 1; 
        esp_now_send(RX_MAC_ADDRESS, (uint8_t *)&payload, sizeof(payload));
        vTaskDelay(pdMS_TO_TICKS(100));

        uint32_t idle_time_sec = 0;
        while (idle_time_sec < 600) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            idle_time_sec++;

            if (rtc_gpio_get_level(PIR_SENSOR_PIN) == 1) {
                idle_time_sec = 0;
            }
        }

        ESP_LOGI(TAG, "10분 경과, DeepSleep 시작");
        payload.command = 2;
        esp_now_send(RX_MAC_ADDRESS, (uint8_t *)&payload, sizeof(payload));
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 초기 부팅, PIR만 세팅
    else {
        ESP_LOGI(TAG, "초기 부팅. PIR 대기 모드로 변경합니다.");
        esp_sleep_enable_ext1_wakeup(1ULL << PIR_SENSOR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    }
    esp_sleep_enable_ext1_wakeup(1ULL << PIR_SENSOR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_start();
}