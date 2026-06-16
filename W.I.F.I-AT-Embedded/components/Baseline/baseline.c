#include "baseline.h"
#include "common_struct.h"
#include "originFunc.h"

csi_baseline_t g_baseline;
volatile bool g_baseline_reset_req = false;
static const char *TAG = "Baseline";

esp_err_t baseline_init(csi_baseline_t *bf) {
    if (!bf) {
        return ESP_FAIL;
    }

    memset(bf->sum, 0, sizeof(bf->sum));
    memset(bf->baseline, 0, sizeof(bf->baseline));
    bf->sample_count = 0;
    bf->ready = false;

    ESP_LOGI(TAG, "Baseline 재학습 종료, 완료 메시지 송신 / topic : wifi/device01/baseline/status");
    mqtt_publish("wifi/device01/baseline/status", "BASELINEDONE", 1, 3);
    return ESP_OK;
}

void baseline_update(csi_baseline_t *bf, const float *amp) {
    if (!bf || !amp || bf->ready) {
        return;
    }

    for (int i = 0; i < CSI_N_SUBCARRIER; i++) {
        bf->sum[i] += amp[i];
    }
    bf->sample_count++;

    if (bf->sample_count >= BASELINE_CALIB_FRAMES) {
        for (int i = 0; i < CSI_N_SUBCARRIER; i++) {
            bf->baseline[i] = bf->sum[i] / (float)bf->sample_count;
        }
        bf->ready = true;
    }
}

void baseline_refresh(csi_baseline_t *bf, const float *amp) {
    if (!bf || !amp || !bf->ready) {
        return;
    }

    const float temp = BASELINE_EMA_ALPHA;
    for (int i = 0; i < CSI_N_SUBCARRIER; i++) {
        bf->baseline[i] += temp * (amp[i] - bf->baseline[i]);
    }
}

bool baseline_apply(const csi_baseline_t *bf, const float *amp, float *out) {
    if (!bf || !amp || !out) {
        return false;
    }
    if (!bf->ready) {
        memset(out, 0, sizeof(float) * CSI_N_SUBCARRIER);
        return false;
    }

    for (int i = 0; i < CSI_N_SUBCARRIER; i++) {
        out[i] = amp[i] - bf->baseline[i];
    }
    return true;
}

float baseline_motion_energy(const csi_baseline_t *bf, const float *amp) {
    if (!bf || !amp || !bf->ready) {
        return 0.0f;
    }

    float acc = 0.0f;
    for (int i = 0; i < CSI_N_SUBCARRIER; i++) {
        acc += fabsf(amp[i] - bf->baseline[i]);
    }
    return acc / (float)CSI_N_SUBCARRIER;
}

bool baseline_is_ready(const csi_baseline_t *bf) {
    return bf && bf->ready;
}