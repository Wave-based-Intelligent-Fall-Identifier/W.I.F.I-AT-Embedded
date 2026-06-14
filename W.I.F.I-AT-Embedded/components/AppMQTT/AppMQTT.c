#include "headers.h"
#include "originFunc.h"
#include "APconfig.h"

static const char *TAG = "App MQTT";
#define BROKER_ADDRESS_URI "mqtt://192.168.0.10:1883"

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_mqtt_connected = false;   

esp_err_t mqtt_publish(const char* topic, const char* data, int qos) {
    if (s_client == NULL || !s_mqtt_connected) {
        ESP_LOGW(TAG, "현재 MQTT 미연결 상태, publish 스킵: %s", topic);
        return ESP_FAIL;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, data, 0, qos, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "publish 실패: %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "새로운 메시지 발행, msg_id=%d", msg_id);
    return ESP_OK;
}

static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    
    ESP_LOGD(TAG, "사용 가능 heap size : %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED, MQTT 연결 성공");
        s_mqtt_connected = true;

        // Broker 구독
        esp_mqtt_client_subscribe(client, "wify/device01/command", 1);
        mqtt_publish( "wify/device01/status", "online", 1);
        network_status();
        network_settings();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED, MQTT 연결 해제");
        s_mqtt_connected = false;
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "TOPIC = %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DATA = %.*s", event->data_len, event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR, MQTT 이벤트 에러 발생");
        break;

    default:
        ESP_LOGI(TAG, "예측되지 않은 event id : %d", event->event_id);
        break;
    }
}

void mqtt5_init(void* pvParameters) {
   esp_mqtt_client_config_t mqtt5_cfg = {
        .broker.address.uri = BROKER_ADDRESS_URI,
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.disable_auto_reconnect = false, // 보드 재연결 활성화
        .credentials.username = "Piuda ESP32 board",
        .credentials.authentication.password = "dsmpiuda2026",
        .session.last_will.topic = "wify/device01/status",
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = 7,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    s_client = esp_mqtt_client_init(&mqtt5_cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt5_event_handler, NULL);
    esp_mqtt_client_start(s_client);
}

// 발행 데이터에 따른 행동 처리 (server -> esp)
// 내 발행 데이터 (esp -> server)