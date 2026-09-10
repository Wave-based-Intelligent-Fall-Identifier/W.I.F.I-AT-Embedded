#include "pir-sensor.h"
#include "originFunc.h"

const static char *TAG = "Pir-Sensor";
const static uint8_t RX_MAC_ADDRESS[6] = AP_MAC_ADDRESS;

void pir_sensor(void* pvParameters) {
    esp_err_t err;
    espnow_payload_t payload = {0};
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint8_t retry_count;

    // PIR이 사람을 감지함, 10분 타이머 리셋 
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "사람 감지, 10분간 CSI 전송 모드 유지");
        err = mqtt_wait_connected(5000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "MQTT재연결 실패");
        }
        mqtt_publish(WIFY_TOPIC("/restroom"), "ACT", 1, 3);

        if (xSemaphoreTake(nowMutex, portMAX_DELAY) == pdTRUE) {
            payload.command = 1; 
            esp_now_send(RX_MAC_ADDRESS, (uint8_t *)&payload, sizeof(payload));
            xSemaphoreGive(nowMutex);
        }
        ESP_LOGI(TAG, "PIR Mutex 확보");
        
        vTaskDelay(pdMS_TO_TICKS(100));

        uint32_t idle_time_ms = 0;
        uint32_t MAX_IDLE_TIME_MS = 600000;

        while (idle_time_ms < MAX_IDLE_TIME_MS) {
            vTaskDelay(pdMS_TO_TICKS(100));
            idle_time_ms += 100;

            if (gpio_get_level(PIR_SENSOR_PIN) == 1) {
                idle_time_ms = 0;
            }

            ESP_LOGI(TAG, "대기 중... %lu초 경과", idle_time_ms / 1000);
        }

        ESP_LOGI(TAG, "10분 경과, DeepSleep 시작");
        mqtt_publish(WIFY_TOPIC("/restroom"), "DEACT", 1, 3);
        
        if (xSemaphoreTake(nowMutex, portMAX_DELAY) == pdTRUE) {
            payload.command = 2;
            retry_count = 0;

            while (retry_count < 3) {
                err = esp_now_send(RX_MAC_ADDRESS, (uint8_t *)&payload, sizeof(payload));
                if (err == ESP_OK) { 
                    ESP_LOGI(TAG, "마지막 메시지 전송 성공");
                    break;
                } else {
                    ESP_LOGI(TAG, "마지막 메시지 전송 실패");
                }
                ESP_LOGW(TAG, "마지막 메시지 전송 재시도 (%d/3)", (retry_count + 1));
                retry_count++;
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            xSemaphoreGive(nowMutex); 
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    else {
        ESP_LOGI(TAG, "초기 부팅. PIR 대기 모드로 변경합니다.");
        mqtt_wait_connected(5000);
        mqtt_publish(WIFY_TOPIC("/restroom"), "LOAD", 1, 3);
        esp_sleep_enable_ext1_wakeup(1ULL << PIR_SENSOR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    }
#ifdef TEST
    ESP_LOGW(TAG, "[TEST] 딥슬립 비활성화, 항시 동작 모드 유지");
    vTaskDelete(NULL);
#else
    esp_sleep_enable_ext1_wakeup(1ULL << PIR_SENSOR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_deep_sleep_start();
#endif
}