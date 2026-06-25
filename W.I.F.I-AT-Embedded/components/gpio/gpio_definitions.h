#ifndef GPIO_DEFINITIONS_H
#define GPIO_DEFINITIONS_H

#include <stdint.h>

#ifndef GPIO_BASE_ADDR
#define GPIO_BASE_ADDR 0x3FF44000 
#endif

#define PIR_SENSOR_PIN 4

// 보드 내장 LED 핀(대부분 ESP32 보드 GPIO2). 보드가 다르면 이 값만 바꾸면 됨.
#define STATUS_LED_PIN 2

// bank0
#define GPIO_ENABLE_W1TC_REG    (*(volatile uint32_t *)(GPIO_BASE_ADDR + 0x0028)) // 0 ~ 31
#define GPIO_IN_REG             (*(volatile uint32_t *)(GPIO_BASE_ADDR + 0x003C)) // 0 ~ 31

#define FAST_GPIO_INPUT_EN(pin) (GPIO_ENABLE_W1TC_REG = (1UL << (pin)))
#define FAST_GPIO_READ(pin)     ((GPIO_IN_REG >> (pin)) & 1UL)

void gpio_pin_init(void);

/** 내장 LED 로 AP 접속 상태 표시 태스크(접속=느린 하트비트, 검색=빠른 깜빡임) */
void status_led_task(void* pvParameters);

#endif