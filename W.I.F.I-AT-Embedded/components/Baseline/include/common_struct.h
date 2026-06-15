#ifndef COMMON_STRUCT_H
#define COMMON_STRUCT_H

#include <stdint.h>
#include <stdbool.h>

#ifndef CSI_N_SUBCARRIER
// WiFi 채널 분리
#define CSI_N_SUBCARRIER 52
#endif

#ifndef BASELINE_CALIB_FRAMES
#define BASELINE_CALIB_FRAMES 600
#endif

#ifndef BASELINE_EMA_ALPHA
#define BASELINE_EMA_ALPHA 0.01f
#endif

typedef struct {
    float    sum[CSI_N_SUBCARRIER];
    float    baseline[CSI_N_SUBCARRIER];
    uint16_t sample_count;  
    bool     ready;
} csi_baseline_t;

/**
 * @brief baseline 초기화 함수
 * @param csi_baseline_t *bf
 * @return esp_err_t
 */
esp_err_t baseline_init(csi_baseline_t *bf);

#endif 