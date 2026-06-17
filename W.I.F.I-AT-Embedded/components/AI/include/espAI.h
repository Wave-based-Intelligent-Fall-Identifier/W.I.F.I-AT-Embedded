#ifndef ESP_AI_TASK
#define ESP_AI_TASK

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "common_struct.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#ifndef RESIDUAL_RING_LEN
#define RESIDUAL_RING_LEN 64
#endif

#ifndef FALL_INFER_STRIDE
#define FALL_INFER_STRIDE 16
#endif

#define FALL_INACTIVITY_EPS 0.5f

/**
 * @brief AI 추론 태스크
 * @param void* pvParameter
 * @return void
 */
void esp_ai_task(void* pvParameter);

typedef struct {
    float    frames[RESIDUAL_RING_LEN][CSI_N_SUBCARRIER];
    uint16_t head;
    uint16_t count;
} residual_ring_t;

#endif