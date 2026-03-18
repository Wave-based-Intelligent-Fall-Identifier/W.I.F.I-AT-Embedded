#pragma once

#define TEN_MINUTES_IN_US (10ULL * 60 * 1000000ULL)

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"

#include "driver/rtc_io.h"
#include "gpio_definitions.h"

typedef struct {
    uint8_t command;
} espnow_payload_t;

void pir_sensor(void* pvParameters);