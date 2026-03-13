#include "pir-sensor.h"

const static char *TAG = "Pir-Sensor";

void pir_sensor(void* pvParameters) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "사람 감지, Deep Sleep해제");
    }

    else {
        esp_sleep_enable_ext1_wakeup(1ULL << PIR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);

        ESP_LOGI(TAG, "PIR 감지 대기... Deep Sleep시작!");
        esp_deep_sleep_start();
    }
}