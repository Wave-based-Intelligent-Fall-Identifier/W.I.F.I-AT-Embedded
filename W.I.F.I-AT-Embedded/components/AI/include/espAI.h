#ifndef ESP_AI_TASK
#define ESP_AI_TASK

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"


/**
 * @brief AI 추론 태스크
 * @param void* pvParameter
 * @return void
 */
void esp_ai_task(void* pvParameter);

#endif