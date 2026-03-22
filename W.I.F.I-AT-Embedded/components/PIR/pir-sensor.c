#include "pir-sensor.h"

const static char *TAG = "Pir-Sensor";
const static uint8_t RX_MAC_ADDRESS[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void pir_sensor(void* pvParameters) {
    uint32_t retry_count = 0;
    esp_err_t err;
    espnow_payload_t payload = {0};
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    // PIR이 사람을 감지함, 10분 타이머 리셋 
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "사람 감지, 10분간 CSI 전송 모드 유지");

        if (xSemaphoreTake(nowMutex, portMAX_DELAY) == pdTRUE) {
            payload.command = 1; 
            esp_now_send(RX_MAC_ADDRESS, (uint8_t *)&payload, sizeof(payload));
            xSemaphoreGive(nowMutex);
        }
        
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
        if (xSemaphoreTake(nowMutex, portMAX_DELAY) == pdTRUE) {
            payload.command = 2;
            retry_count = 0;

            while (retry_count < 3) {
                err = esp_now_send(RX_MAC_ADDRESS, (uint8_t *)&payload, sizeof(payload));
                if (err == ESP_FAIL) {
                    ESP_LOGI(TAG, "마지막 메시지 전송 실패");
                }
                else {
                    ESP_LOGI(TAG, "마지막 메시지 전송 성공");
                    break;
                }
                ESP_LOGW(TAG, "마지막 메시지 전송 재시도 (%d/3)", retry_count);
                retry_count++;
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }

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