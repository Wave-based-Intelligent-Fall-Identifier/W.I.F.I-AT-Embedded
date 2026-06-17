#include "espAI.h"
#include "common_struct.h"
#include "baseline.h"
#include "espnowAP.h"
#include "gpio_definitions.h"
#include "driver/gpio.h"
#include "originFunc.h"

static const char *TAG = "ESP-AI";
static residual_ring_t s_residual_ring;
static uint16_t s_infer_counter = 0;

static void residual_ring_reset(void) {
    s_residual_ring.head = 0;
    s_residual_ring.count = 0;
    s_infer_counter = 0;
}

static void residual_ring_push(const float *residual) {
    memcpy(s_residual_ring.frames[s_residual_ring.head], residual, sizeof(float) * CSI_N_SUBCARRIER);
    s_residual_ring.head = (s_residual_ring.head + 1) % RESIDUAL_RING_LEN;
    if (s_residual_ring.count < RESIDUAL_RING_LEN) {
        s_residual_ring.count++;
    }
}

static const float *residual_ring_get(uint16_t age) {
    if (age >= s_residual_ring.count) {
        return NULL;
    }
    int idx = (int)s_residual_ring.head - 1 - (int)age;
    while (idx < 0) {
        idx += RESIDUAL_RING_LEN;
    }
    return s_residual_ring.frames[idx];
}

static bool extract_features(float out[5]) {
    if (s_residual_ring.count < RESIDUAL_RING_LEN) {
        return false;
    }

    float base_mean = 0.0f;
    for (int i = 0; i < CSI_N_SUBCARRIER; i++) {
        base_mean += g_baseline.baseline[i];
    }
    base_mean /= (float)CSI_N_SUBCARRIER;

    float strength[RESIDUAL_RING_LEN];
    for (int t = 0; t < RESIDUAL_RING_LEN; t++) {
        int idx = (s_residual_ring.head + t) % RESIDUAL_RING_LEN;
        float acc = 0.0f;
        for (int i = 0; i < CSI_N_SUBCARRIER; i++) {
            acc += s_residual_ring.frames[idx][i];
        }
        strength[t] = acc / (float)CSI_N_SUBCARRIER + base_mean;
    }

    float sum = 0.0f, mn = strength[0], mx = strength[0];
    for (int t = 0; t < RESIDUAL_RING_LEN; t++) {
        sum += strength[t];
        if (strength[t] < mn) mn = strength[t];
        if (strength[t] > mx) mx = strength[t];
    }
    float mean = sum / (float)RESIDUAL_RING_LEN;

    float var = 0.0f;
    for (int t = 0; t < RESIDUAL_RING_LEN; t++) {
        float d = strength[t] - mean;
        var += d * d;
    }
    float stddev = sqrtf(var / (float)RESIDUAL_RING_LEN);

    float motion = 0.0f;
    int inactivity = 0;
    bool still = true;
    for (int t = RESIDUAL_RING_LEN - 1; t >= 1; t--) {
        float diff = fabsf(strength[t] - strength[t - 1]);
        motion += diff;
        if (still && diff < FALL_INACTIVITY_EPS) {
            inactivity++;
        } else {
            still = false;
        }
    }

    out[0] = mean;
    out[1] = stddev;
    out[2] = mx - mn;
    out[3] = motion;
    out[4] = (float)inactivity;
    return true;
}

static bool fall_infer(const float *features) {
    (void)features;
    return false;
}

void esp_ai_task(void* pvParameter) {
    csi_raw_t raw;
    float amp[CSI_N_SUBCARRIER];
    float residual[CSI_N_SUBCARRIER];
    float energy;

    ESP_LOGI(TAG, "CSI 처리 task 시작, baseline 캘리브레이션 대기");

    while (1) {
        if (g_csi_queue == NULL || xQueueReceive(g_csi_queue, &raw, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (g_baseline_reset_req) {
            g_baseline_reset_req = false;
            baseline_init(&g_baseline);
            residual_ring_reset();

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
        residual_ring_push(residual);

        if (++s_infer_counter >= FALL_INFER_STRIDE) {
            s_infer_counter = 0;
            float features[5];
            if (extract_features(features)) {
                if (fall_infer(features)) {
                    mqtt_publish("wify/device01/fall", "FALL", 1, 3);
                }
            }
        }

        energy = baseline_motion_energy(&g_baseline, amp);
        if (energy < BASELINE_REFRESH_THRESHOLD && gpio_get_level(PIR_SENSOR_PIN) == 0) {
            baseline_refresh(&g_baseline, amp);
        }
    }
}

