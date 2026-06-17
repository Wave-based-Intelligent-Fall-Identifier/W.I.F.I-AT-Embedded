#include "baseline.h"
#include "common_struct.h"
#include "originFunc.h"

csi_baseline_t g_baseline;
volatile bool g_baseline_reset_req = false;
static const char *TAG = "Baseline";
nvs_handle_t nvs_mem_handle;

#define BASELINE_NVS_NS  "storage"
#define BASELINE_NVS_KEY "data"

static esp_err_t baseline_nvs_open(void) {
    if (nvs_mem_handle != 0) {
        return ESP_OK;
    }
    return nvs_open(BASELINE_NVS_NS, NVS_READWRITE, &nvs_mem_handle);
}

static esp_err_t baseline_save_nvs(const csi_baseline_t *bf) {
    esp_err_t err = baseline_nvs_open();
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs_mem_handle, BASELINE_NVS_KEY, bf, sizeof(*bf));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_mem_handle);
    }
    return err;
}

esp_err_t baseline_load_nvs(csi_baseline_t *bf) {
    if (!bf) {
        return ESP_FAIL;
    }
    esp_err_t err = baseline_nvs_open();
    if (err != ESP_OK) {
        return err;
    }
    size_t len = sizeof(*bf);
    err = nvs_get_blob(nvs_mem_handle, BASELINE_NVS_KEY, bf, &len);
    if (err == ESP_OK && len == sizeof(*bf)) {
        return ESP_OK;
    }
    return (err == ESP_OK) ? ESP_FAIL : err;
}

esp_err_t baseline_init(csi_baseline_t *bf) {
    if (!bf) {
        return ESP_FAIL;
    }

    memset(bf->sum, 0, sizeof(bf->sum));
    memset(bf->baseline, 0, sizeof(bf->baseline));
    bf->sample_count = 0;
    bf->ready = false;

    ESP_ERROR_CHECK(baseline_nvs_open());

    ESP_LOGI(TAG, "Baseline 재학습 시작, 시작 메시지 송신 / topic : wify/device01/baseline/status");
    mqtt_publish("wify/device01/baseline/status", "BASELINESTART", 1, 3);
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

        ESP_LOGI(TAG, "Baseline 캘리브레이션 완료, 완료 메시지 송신 / topic : wify/device01/baseline/status");
        mqtt_publish("wify/device01/baseline/status", "BASELINEDONE", 1, 3);
        baseline_save_nvs(bf);
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