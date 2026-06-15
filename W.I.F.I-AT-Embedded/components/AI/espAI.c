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

    ESP_LOGI(TAG, "CSI 처리 task 시작, baseline 캘리브레이션 대기");

    while (1) {
        if (g_csi_queue == NULL ||
            xQueueReceive(g_csi_queue, &raw, portMAX_DELAY) != pdTRUE) {
            continue;
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
            baseline_update(&g_baseline, amp);
            if (baseline_is_ready(&g_baseline)) {
                ESP_LOGI(TAG, "baseline ready=true (캘리브레이션 완료)");
            }
            continue;
        }

        baseline_apply(&g_baseline, amp, residual);
        // TODO: residual 을 링버퍼에 적재 후 AI 추론(낙상 분류)에 사용

        // --- 움직임 강도 ---
        float energy = baseline_motion_energy(&g_baseline, amp);

        // --- 빈방이 확실할 때만 baseline 을 천천히 갱신 ---
        // 움직임이 임계값 미만 AND PIR 미감지(LOW=사람 없음)일 때만 호출.
        // 사람이 정지해 있어도 PIR 이 HIGH 면 갱신을 막아 '쓰러진 사람'을 배경으로 흡수하지 않는다.
        if (energy < BASELINE_REFRESH_THRESHOLD && gpio_get_level(PIR_SENSOR_PIN) == 0) {
            baseline_refresh(&g_baseline, amp);
        }
    }
}
