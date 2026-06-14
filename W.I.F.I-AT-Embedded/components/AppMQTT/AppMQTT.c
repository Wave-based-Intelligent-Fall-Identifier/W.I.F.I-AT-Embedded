#include "headers.h"
#include "originFunc.h"

static const char *TAG = "App MQTT";

static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    
    ESP_LOGD(TAG, "사용 가능 heap size : %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED, MQTT 연결 성공");
        esp_mqtt_client_subscribe(client, "wify/device01/command", 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED, MQTT 연결 해제");
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "TOPIC = %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DATA = %.*s", event->data_len, event->data);
        break;

    default:
        ESP_LOGI(TAG, "예측되지 않은 event id : %d", event->event_id);
        break;
    }
}

// 연결 끊김 대처 함수
// 서버가 보드 구독 시 연결 메시지 발행
// 연결 시 이벤트 수정
// 핸들러 등록 
// 시작
// wifi 연결
// 클라이언트 초기화 생성