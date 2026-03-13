#pragma once

#define PIR_PIN GPIO_NUM_33
#define TEN_MINUTES_IN_US (10ULL * 60 * 1000000ULL)

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_sleep.h"
#include "esp_log.h"

#include "driver/rtc_io.h"