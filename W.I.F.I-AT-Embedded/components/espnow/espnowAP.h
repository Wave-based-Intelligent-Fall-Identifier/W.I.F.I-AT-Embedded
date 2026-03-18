#pragma once 

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_err.h"

#include "nvs_flash.h"
#include "pir-sensor.h"
#include "APconfig.h"

#define  CONNECTED_BIT      BIT0
#define  GOT_IP_BIT         BIT2
#define  FAIL_BIT           BIT4

#define WIFI_SSID      "k"
#define WIFI_PASS      "ericeric0223"
#define MAXIMUM_RETRY  5

esp_err_t wifiInit(void);
static void wifiHandler(void *args, esp_event_base_t eventBase, int32_t eventId, void* eventData);
void espnow_csi_send(void* pvParameter);