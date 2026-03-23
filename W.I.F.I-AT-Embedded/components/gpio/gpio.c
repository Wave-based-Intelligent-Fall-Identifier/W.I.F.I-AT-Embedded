#include "gpio_definitions.h"

void gpio_pin_init(void) {
    FAST_GPIO_INPUT_EN(PIR_SENSOR_PIN);
}