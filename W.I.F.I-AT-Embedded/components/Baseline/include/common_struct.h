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

#ifndef BASELINE_REFRESH_THRESHOLD
#define BASELINE_REFRESH_THRESHOLD 3.0f
#endif

#ifndef CSI_RAW_MAX_LEN
#define CSI_RAW_MAX_LEN 512
#endif

#ifndef CSI_QUEUE_LEN
// CSI 콜백 -> 처리 task 큐 깊이
#define CSI_QUEUE_LEN 16
#endif

typedef struct {
    float    sum[CSI_N_SUBCARRIER];
    float    baseline[CSI_N_SUBCARRIER];
    uint16_t sample_count;
    bool     ready;
} csi_baseline_t;

// CSI 콜백 -> 처리 task 로 raw I/Q 를 넘기는 구조체
typedef struct {
    int8_t   buf[CSI_RAW_MAX_LEN];
    uint16_t len;
} csi_raw_t;

/**
 * @brief baseline 초기화 함수
 * @param csi_baseline_t *bf
 * @return esp_err_t
 */
esp_err_t baseline_init(csi_baseline_t *bf);

 
/**
 * @brief baseline 업데이트 함수
 * @param csi_baseline_t *bf, const float *amp
 * @return void
 * @note PIR 사람 감지 센서와 자원 공유 필요
 */
void baseline_update(csi_baseline_t *bf, const float *amp);

/**
 * @brief 빈방 baseline 보정 함수
 * @param csi_baseline_t *bf, const float *amp
 * @return void
 * @note PIR 사람 감지 센서와 자원 공유 필요
 */
void baseline_refresh(csi_baseline_t *bf, const float *amp);
 
/**
 * @brief CSI 감산 함수 
 * @details 실시간 값 - 배경
 * @param const csi_baseline_t *bf, const float *amp, float *out
 * @return true or false
 */
bool baseline_apply(const csi_baseline_t *bf, const float *amp, float *out);
 

/**
 * @brief 움직인 대략적 계산 함수
 * @param const csi_baseline_t *bf, const float *amp
 * @return motion score
 */
float baseline_motion_energy(const csi_baseline_t *bf, const float *amp);
 
/**
 * @brief BASELINE_CALIB_FRAMES가 모두 찼는지 검사하는 함수
 */
bool baseline_is_ready(const csi_baseline_t *bf);
 
extern csi_baseline_t g_baseline;
#endif 