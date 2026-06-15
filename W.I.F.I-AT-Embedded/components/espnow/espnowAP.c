#include "espnowAP.h"
#include "common_struct.h"
#define WIFI_CONNECTED_BIT BIT0

const static char* TAG = "ESP-NOW-AP";
static EventGroupHandle_t wifiEventGroup;
uint8_t networkFlag = 0;

QueueHandle_t g_csi_queue = NULL;

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

static void csi_rx_cb(void *ctx, wifi_csi_info_t *info) {
    if (!info || !info->buf || info->len <= 0 || g_csi_queue == NULL) {
        return;
    }

    static bool logged = false;
    if (!logged) {
        logged = true;
        ESP_LOGI(TAG, "CSI len=%d bytes (subcarriers=%d)", info->len, info->len / 2);
    }

    csi_raw_t raw;
    uint16_t n = (info->len > CSI_RAW_MAX_LEN) ? CSI_RAW_MAX_LEN : info->len;
    memcpy(raw.buf, info->buf, n);
    raw.len = n;

    xQueueSend(g_csi_queue, &raw, 0);
}

esp_err_t csi_recv_init(void) {
    esp_err_t err;

    g_csi_queue = xQueueCreate(CSI_QUEUE_LEN, sizeof(csi_raw_t));
    if (g_csi_queue == NULL) {
        ESP_LOGE(TAG, "CSI 큐 생성 실패");
        return ESP_ERR_NO_MEM;
    }

    wifi_csi_config_t csi_config = {
        .lltf_en           = true,
        .htltf_en          = true,
        .stbc_htltf2_en    = true,
        .ltf_merge_en      = true,
        .channel_filter_en = true,
        .manu_scale        = false,
        .shift             = 0,
        .dump_ack_en       = false,
    };

    err = esp_wifi_set_csi_config(&csi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI config 실패: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_csi_rx_cb(csi_rx_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI rx 콜백 등록 실패: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI 활성화 실패: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "CSI 수신 초기화 성공");
    return ESP_OK;
}