#include "espAI.h"
#include "common_struct.h"
#include "baseline.h"
#include "espnowAP.h"
#include "gpio_definitions.h"
#include "driver/gpio.h"
#include "originFunc.h"
#include "gru_functions.h"

static const char *TAG = "ESP-AI";

/* =========================================================================
 *  GRU 낙상 판정 연동 설정 (AI 담당자 export: gru_weights.h + gru_functions.c)
 *
 *  ⚠️ 아래 3개는 학습 파이프라인과 반드시 맞춰야 하는 "미확정" 항목이다.
 *     보드 없이 확정 불가 → AI 담당자 확인 후 값만 바꾸면 됨(로직 변경 불필요).
 * ------------------------------------------------------------------------- */

/* [극성] gru_functions.c 주석 = "probability of normal (class 1)".
 *   1 이면 출력이 높을수록 정상(낙상 확률 = 1 - out).
 *   학습 라벨이 반대(1=낙상)라면 0 으로 바꾼다. */
#define GRU_OUTPUT_IS_P_NORMAL 1

/* [정규화] 학습 때 입력을 표준화했다면 여기서 동일 변환을 해줘야 한다.
 *   현재는 raw amplitude 그대로 사용(항등). mean/std 를 받으면 apply_input_norm 수정. */
#define GRU_INPUT_NORMALIZE 0

/* [입력 서브캐리어] 학습셋(csi_dataset.csv)이 sc16~sc27(12개)만 사용 →
 *   raw CSI 서브캐리어 인덱스 16부터 GRU_INPUT_DIM(=12)개를 그대로 선택. */
#define GRU_SC_BASE  16

/* [임계값] 낙상 확률(p_fall) 기준 3단계. 검증 후 튜닝 대상.
 *   모델 결정경계: p_normal < 0.5 == p_fall >= 0.5 -> 낙상. DANGER를 여기에 맞춤. */
#define GRU_PFALL_DANGER   0.50f   /* p_fall >= 0.50 -> DAN(낙상 확정) */
#define GRU_PFALL_WARNING  0.40f   /* 0.40 <= p_fall < 0.50 -> WARN(의심) */

/* 추론 주기: 새 프레임 GRU_INFER_STRIDE개마다 1회 판정 */
#define GRU_INFER_STRIDE   FALL_INFER_STRIDE

/* AI 판단 발행 토픽/페이로드 (앱: wify/{id}/AI, DAN/WARN/NOR — WifyTopics.kt 와 1:1) */
#define AI_TOPIC     WIFY_TOPIC("/AI")
#define AI_PL_DANGER  "DAN"
#define AI_PL_WARNING "WARN"
#define AI_PL_NORMAL  "NOR"

/* =========================================================================
 *  GRU 입력 윈도 (raw amplitude, 시간순 [oldest..newest], baseline 미감산)
 * ------------------------------------------------------------------------- */
static float    s_win[GRU_SEQ_LEN][GRU_INPUT_DIM];  /* 항상 시간 오름차순 유지 */
static uint16_t s_win_count = 0;                    /* 채워진 프레임 수(<=SEQ_LEN) */
static uint16_t s_infer_counter = 0;

typedef enum { AI_JUDG_NORMAL = 0, AI_JUDG_WARNING = 1, AI_JUDG_DANGER = 2 } ai_judg_t;
static int s_last_judg = -1;  /* 에지 검출용: 직전 발행 판단(-1=미발행) */

static void gru_window_reset(void) {
    s_win_count = 0;
    s_infer_counter = 0;
}

/* 최신 프레임을 [SEQ_LEN-1] 에 넣고 나머지는 한 칸씩 당긴다(항상 시간순 정렬). */
static void gru_window_push(const float *frame64) {
    if (s_win_count < GRU_SEQ_LEN) {
        memcpy(s_win[s_win_count], frame64, sizeof(float) * GRU_INPUT_DIM);
        s_win_count++;
        return;
    }
    memmove(s_win[0], s_win[1], sizeof(float) * GRU_INPUT_DIM * (GRU_SEQ_LEN - 1));
    memcpy(s_win[GRU_SEQ_LEN - 1], frame64, sizeof(float) * GRU_INPUT_DIM);
}

/* 학습 시 정규화가 있었다면 여기서 동일 변환(현재 항등). */
static inline void apply_input_norm(float *frame64) {
#if GRU_INPUT_NORMALIZE
    /* TODO(AI): mean[64]/std[64] 를 받아 (x-mean)/std 적용 */
    (void)frame64;
#else
    (void)frame64;
#endif
}

/* 판단 발행 — 상태 전이(에지)에서만 (앱도 에지 필터링하지만 브로커 트래픽 절감). */
static void ai_publish(ai_judg_t judg) {
    if ((int)judg == s_last_judg) return;
    s_last_judg = (int)judg;
    const char *pl = (judg == AI_JUDG_DANGER)  ? AI_PL_DANGER
                   : (judg == AI_JUDG_WARNING) ? AI_PL_WARNING
                                               : AI_PL_NORMAL;
    mqtt_publish(AI_TOPIC, pl, 1, 3);
    ESP_LOGI(TAG, "AI 판단 발행: %s", pl);
}

static ai_judg_t pfall_to_judg(float p_fall) {
    if (p_fall >= GRU_PFALL_DANGER)  return AI_JUDG_DANGER;
    if (p_fall >= GRU_PFALL_WARNING) return AI_JUDG_WARNING;
    return AI_JUDG_NORMAL;
}

void esp_ai_task(void* pvParameter) {
    csi_raw_t raw;
    float amp[CSI_N_SUBCARRIER];        /* baseline 유지용(기존 파이프라인) */
    float gamp[GRU_INPUT_DIM];          /* 모델 입력용 raw amplitude(64) */
    float energy;

    ESP_LOGI(TAG, "CSI 처리 task 시작, baseline 캘리브레이션 대기");

    static uint32_t s_diag_rx = 0;   /* [DIAG] 임시 진단 */
    while (1) {
        if (g_csi_queue == NULL) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (xQueueReceive(g_csi_queue, &raw, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGW(TAG, "[DIAG] CSI 2초간 없음 (PIR=%d, base_ready=%d)",
                     gpio_get_level(PIR_SENSOR_PIN), baseline_is_ready(&g_baseline));
            continue;
        }
        if (++s_diag_rx % 50 == 0) {
            ESP_LOGI(TAG, "[DIAG] CSI rx=%u, PIR=%d, base_ready=%d, base_cnt=%u",
                     (unsigned)s_diag_rx, gpio_get_level(PIR_SENSOR_PIN),
                     baseline_is_ready(&g_baseline), (unsigned)g_baseline.sample_count);
        }

        if (g_baseline_reset_req) {
            g_baseline_reset_req = false;
            baseline_init(&g_baseline);
            gru_window_reset();
            ESP_LOGI(TAG, "Baseline 재탐지 시작");
        }

        int pairs = raw.len / 2;  /* 복소쌍(=서브캐리어) 개수 */

        /* baseline 파이프라인용 진폭(CSI_N_SUBCARRIER 폭) */
        int n = (pairs > CSI_N_SUBCARRIER) ? CSI_N_SUBCARRIER : pairs;
        for (int i = 0; i < n; i++) {
            float im = (float)raw.buf[2 * i];
            float re = (float)raw.buf[2 * i + 1];
            amp[i] = sqrtf(re * re + im * im);
        }
        for (int i = n; i < CSI_N_SUBCARRIER; i++) {
            amp[i] = 0.0f;
        }

        /* 모델 입력용 진폭(sc16~sc27 = 12개, raw — baseline 감산 안 함).
         * 학습셋과 동일하게 서브캐리어 GRU_SC_BASE..+GRU_INPUT_DIM 만 선택. */
        for (int i = 0; i < GRU_INPUT_DIM; i++) {
            int sc = GRU_SC_BASE + i;
            if (sc < pairs) {
                float im = (float)raw.buf[2 * sc];
                float re = (float)raw.buf[2 * sc + 1];
                gamp[i] = sqrtf(re * re + im * im);
            } else {
                gamp[i] = 0.0f;
            }
        }

        /* 캘리브레이션(빈방 baseline 수집) 중에는 추론하지 않음 */
        if (!baseline_is_ready(&g_baseline)) {
            if (gpio_get_level(PIR_SENSOR_PIN) == 0) {
                baseline_update(&g_baseline, amp);
                if (baseline_is_ready(&g_baseline)) {
                    ESP_LOGI(TAG, "baseline ready=true (캘리브레이션 완료)");
                }
            }
            continue;
        }

        /* --- GRU 추론 경로 --- */
        apply_input_norm(gamp);
        gru_window_push(gamp);

        if (++s_infer_counter >= GRU_INFER_STRIDE) {
            s_infer_counter = 0;
            if (s_win_count >= GRU_SEQ_LEN) {
                float out = gru_predict((const float (*)[GRU_INPUT_DIM])s_win);
#if GRU_OUTPUT_IS_P_NORMAL
                float p_fall = 1.0f - out;
#else
                float p_fall = out;
#endif
                ai_publish(pfall_to_judg(p_fall));
            }
        }

        /* 빈방 감지 시 baseline 미세 보정(기존 로직 유지) */
        energy = baseline_motion_energy(&g_baseline, amp);
        if (energy < BASELINE_REFRESH_THRESHOLD && gpio_get_level(PIR_SENSOR_PIN) == 0) {
            baseline_refresh(&g_baseline, amp);
        }
    }
}
