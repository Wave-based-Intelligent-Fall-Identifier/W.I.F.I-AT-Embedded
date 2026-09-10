#include "headers.h"
#include "originFunc.h"
#include "APconfig.h"

static const char *TAG = "MQTT Send";

void heartbeat_task(void* pvParameter) {
    char payload[64];
    while (1) {
        snprintf(payload, sizeof(payload), "{\"alive\":true,\"uptime\":%lld}", esp_timer_get_time() / 1000000); 
        mqtt_publish("wify/device01/heartbeat", payload, 0, 1);
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30초, QoS : 0
    }
}   

// 네트워크 상태
void network_status(void) {
    extern uint8_t networkFlag;
    if (!networkFlag) {
        ESP_LOGE(TAG, "wifi 연결 / 전역 변수 문제 발생");
    }

    char payload[64];
    snprintf(payload, sizeof(payload), "{\"network\":\"%s\"}", networkFlag ? "connect" : "disconnected");
    mqtt_publish("wify/device01/nownetwork/status", payload, 1, 3);
    return;
}

//네트워크 id/passwd 
void network_settings(void) {
     char payload[160];
     snprintf(payload, sizeof(payload),
              "{\"networkid\":\"%s\"}",
              id);

    mqtt_publish("wify/device01/nownetwork", payload, 1, 3);
    return;
}

// 네트워크 재요청 
void network_settings_send_again(void) {
    char payload[64];
    snprintf(payload, sizeof(payload), "AGAIN");

    mqtt_publish("wify/device01/nownetwork/again", payload, 1, 3);
    return;
}