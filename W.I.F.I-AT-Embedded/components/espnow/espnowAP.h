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
#include "sdkconfig.h"

#define  CONNECTED_BIT      BIT0
#define  GOT_IP_BIT         BIT2
#define  FAIL_BIT           BIT4

#define MAXIMUM_RETRY  5

#define CSI_PING_INTERVAL_MS 50

esp_err_t wifiInit(void);
void wifiHandler(void *args, esp_event_base_t eventBase, int32_t eventId, void* eventData);
esp_err_t espnowInit(void);

/**
 * @brief CSI 수신 초기화 (config + rx 콜백 등록 + 활성화 + 큐 생성)
 * @return esp_err_t
 * @note esp_wifi_start() 이후에 호출할 것
 */
esp_err_t csi_recv_init(void);

esp_err_t csi_traffic_init(void);

// CSI 콜백이 raw 를 적재하고 처리 task 가 소비하는 큐
extern QueueHandle_t g_csi_queue;