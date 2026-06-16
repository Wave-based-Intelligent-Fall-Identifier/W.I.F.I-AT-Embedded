#include "espAI.h"
#include "common_struct.h"
#include "baseline.h"
#include "espnowAP.h"
#include "gpio_definitions.h"
#include "driver/gpio.h"

static const char *TAG = "ESP-AI";

void esp_ai_task(void* pvParameter) {
    csi_raw_t raw;
    float amp[CSI_N_SUBCARRIER];
    float residual[CSI_N_SUBCARRIER];
    float energy;

    ESP_LOGI(TAG, "CSI 처리 task 시작, baseline 캘리브레이션 대기");

    while (1) {
        if (g_csi_queue == NULL ||
            xQueueReceive(g_csi_queue, &raw, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // 안전하게 한 번 더 검사 
        if (g_baseline_reset_req) {
            g_baseline_reset_req = false;
            baseline_init(&g_baseline);
            ESP_LOGI(TAG, "Baseline 재탐지 시작"); 
        }

        int n = raw.len / 2;
        if (n > CSI_N_SUBCARRIER) {
            n = CSI_N_SUBCARRIER;
        }
        for (int i = 0; i < n; i++) {
            float im = (float)raw.buf[2 * i];
            float re = (float)raw.buf[2 * i + 1]; 
            amp[i] = sqrtf(re * re + im * im);
        }
        for (int i = n; i < CSI_N_SUBCARRIER; i++) {
            amp[i] = 0.0f;
        }

        if (!baseline_is_ready(&g_baseline)) {
            if (gpio_get_level(PIR_SENSOR_PIN) == 0) {
                baseline_update(&g_baseline, amp);
                if (baseline_is_ready(&g_baseline)) {
                    ESP_LOGI(TAG, "baseline ready=true (캘리브레이션 완료)");
                }
            }
            continue;
        }

        baseline_apply(&g_baseline, amp, residual);
        // TODO: residual 을 링버퍼에 적재 후 AI 추론(낙상 분류)에 사용

        energy = baseline_motion_energy(&g_baseline, amp);
        if (energy < BASELINE_REFRESH_THRESHOLD && gpio_get_level(PIR_SENSOR_PIN) == 0) {
            baseline_refresh(&g_baseline, amp);
        }
    }
}
