#include <stdint.h>

#ifndef GPIO_DEFINITIONS_H
#define GPIO_DEFINITIONS_H

#ifndef GPIO_BASE_ADDR
#define GPIO_BASE_ADDR 0x3FF44000 
#endif

#define PIR_SENSOR_PIN 4

#define GPIO_ENABLE_W1TC_REG (*(volatile uint32_t *)(GPIO_BASE_ADDR + 0x0028))

#define FAST_GPIO_INPUT_EN(pin) (GPIO_ENABLE_W1TC_REG = (1UL << (pin)))

#endif