#include "gpio_definitions.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// espnowAP.c 전역: 공유기(AP) 접속 여부 (1=접속, 0=미접속)
extern uint8_t networkFlag;

void gpio_pin_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIR_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

/**
 * @brief 내장 LED 로 AP 접속(=두 기기 통신) 상태 표시
 *   - 접속됨(networkFlag=1) : 느린 하트비트(켜짐 위주) → "연결 OK"
 *   - 검색 중(networkFlag=0) : 빠른 깜빡임            → "연결 시도 중"
 */
void status_led_task(void* pvParameters) {
    gpio_config_t led = {
        .pin_bit_mask = (1ULL << STATUS_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led);

    while (1) {
        if (networkFlag) {                       // 접속됨 → 느린 하트비트
            gpio_set_level(STATUS_LED_PIN, 1); vTaskDelay(pdMS_TO_TICKS(900));
            gpio_set_level(STATUS_LED_PIN, 0); vTaskDelay(pdMS_TO_TICKS(100));
        } else {                                 // 검색 중 → 빠른 깜빡임
            gpio_set_level(STATUS_LED_PIN, 1); vTaskDelay(pdMS_TO_TICKS(120));
            gpio_set_level(STATUS_LED_PIN, 0); vTaskDelay(pdMS_TO_TICKS(120));
        }
    }
}