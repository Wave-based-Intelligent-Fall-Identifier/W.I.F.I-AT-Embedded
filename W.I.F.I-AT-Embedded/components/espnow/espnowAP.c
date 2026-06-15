#include "espnowAP.h"
#define WIFI_CONNECTED_BIT BIT0

const static char* TAG = "ESP-NOW-AP";
static EventGroupHandle_t wifiEventGroup;
uint8_t networkFlag = 0;

const static uint8_t RX_MAC_ADDRESS[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void wifiHandler(void *args, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi STA 시작, 공유기 접속 시도");
    }
    else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
        networkFlag = 0;
        ESP_LOGI(TAG, "공유기 연결 끊김, 재접속");
        esp_wifi_connect();
    }
    else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
        networkFlag = 1;

        ip_event_got_ip_t* event = (ip_event_got_ip_t*) eventData;
        ESP_LOGI(TAG, "IP 받음: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifiEventGroup, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifiInit(void) {
    esp_err_t err;
    wifiEventGroup = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if(err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 초기화 실패");
        return err;
    }

    wifi_init_config_t wifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifiInitConfig);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 기본 초기화 실패");
        return err;
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiHandler, NULL, &instance_any_id);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "핸들러 등록 실패 (WIFI_EVENT)");
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiHandler, NULL, &instance_got_ip);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "핸들러 등록 실패 (IP_EVENT)");
        return err;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = id,
            .password = passwd,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi 초기화 성공, 연결 대기 중");

    xEventGroupWaitBits(wifiEventGroup, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t espnowInit(void) {
    esp_err_t err;

    uint8_t primary_channel;
    wifi_second_chan_t second_channel;
    esp_wifi_get_channel(&primary_channel, &second_channel);
    ESP_LOGI(TAG, "현재 STA 채널: %d", primary_channel);

    err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW 초기화 실패");
        return err;
    }

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, RX_MAC_ADDRESS, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW peer 추가 실패");
        return err;
    }

    ESP_LOGI(TAG, "ESP-NOW 초기화 성공");
    return ESP_OK;
}

void espnow_csi_send(void* pvParameter) {
    while(1) {
        espnow_payload_t payload;
        payload.command = 0;

        if (xSemaphoreTake(nowMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            esp_now_send(RX_MAC_ADDRESS, (uint8_t *)&payload, sizeof(payload));
            xSemaphoreGive(nowMutex);
        }
        else {
            ESP_LOGE(TAG, "Mutex 획득 실패, CSI 전송 불가");
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}