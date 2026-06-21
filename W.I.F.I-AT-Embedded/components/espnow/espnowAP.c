#include "espnowAP.h"
#include "common_struct.h"
#include "esp_netif.h"
#include "ping/ping_sock.h"
#include "lwip/ip_addr.h"
#define WIFI_CONNECTED_BIT BIT0

const static char* TAG = "ESP-NOW-AP";
static EventGroupHandle_t wifiEventGroup;
uint8_t networkFlag = 0;

QueueHandle_t g_csi_queue = NULL;

const static uint8_t RX_MAC_ADDRESS[6] = AP_MAC_ADDRESS;

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

    EventBits_t bits = xEventGroupWaitBits(wifiEventGroup, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WiFi 연결 대기 시간 초과, 백그라운드에서 재연결 시도 계속");
    }
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

static esp_ping_handle_t s_csi_ping = NULL;

static void csi_ping_noop(esp_ping_handle_t hdl, void *args) {
}

esp_err_t csi_traffic_init(void) {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        ESP_LOGE(TAG, "STA netif 없음, CSI 트래픽 시작 불가");
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.gw.addr == 0) {
        ESP_LOGE(TAG, "게이트웨이 IP 없음, CSI 트래픽 시작 불가");
        return ESP_FAIL;
    }

    ip_addr_t target;
    memset(&target, 0, sizeof(target));
    ip4_addr_set_u32(ip_2_ip4(&target), ip_info.gw.addr);
    IP_SET_TYPE(&target, IPADDR_TYPE_V4);

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = target;
    config.count = ESP_PING_COUNT_INFINITE;
    config.interval_ms = CSI_PING_INTERVAL_MS;
    config.task_stack_size = 3072;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = csi_ping_noop,
        .on_ping_timeout = csi_ping_noop,
        .on_ping_end = csi_ping_noop,
        .cb_args = NULL,
    };

    esp_err_t err = esp_ping_new_session(&config, &cbs, &s_csi_ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI 트래픽 ping 세션 생성 실패: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ping_start(s_csi_ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CSI 트래픽 ping 시작 실패: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "CSI 트래픽 생성 시작 (gateway ping, interval=%dms)", CSI_PING_INTERVAL_MS);
    return ESP_OK;
}