#include "espnowAP.h"
#include "common_struct.h"
#include "esp_netif.h"
#include "ping/ping_sock.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
#include <stdio.h>
#define WIFI_CONNECTED_BIT BIT0

#ifndef CSI_UART_DUMP
#define CSI_UART_DUMP 0
#endif
#ifndef CSI_UART_DUMP_PAIRS
#define CSI_UART_DUMP_PAIRS 64   
#endif

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

#if CSI_UART_DUMP
    static char dump[CSI_UART_DUMP_PAIRS * 2 * 6 + 16];
    int dpairs = raw.len / 2;
    if (dpairs > CSI_UART_DUMP_PAIRS) dpairs = CSI_UART_DUMP_PAIRS;
    int off = snprintf(dump, sizeof(dump), "CSIDUMP [");
    for (int i = 0; i < dpairs && off < (int)sizeof(dump) - 8; i++) {
        off += snprintf(dump + off, sizeof(dump) - off, "%d,%d,",
                        raw.buf[2 * i], raw.buf[2 * i + 1]);
    }
    snprintf(dump + off, sizeof(dump) - off, "]");
    puts(dump);
#endif
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

/**
 * @brief CSI 트래픽 송신 태스크 — 게이트웨이(STA)로 고정 레이트 UDP 일방 송신.
 *   ping(왕복)과 달리 상대 응답이 필요 없어, 정확히 CSI_TX_INTERVAL_MS 간격으로
 *   전파가 나가 STA 의 CSI 가 일정·촘촘하게 생성된다. (UDP 수신자 없어도 전파는 나감)
 */
static void csi_udp_sender_task(void *arg) {
    static const uint8_t payload[32] = {0};   // 더미 페이로드(내용 무관)
    int sock = -1;
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(CSI_TX_PORT);

    ESP_LOGI(TAG, "CSI 트래픽 생성 시작 (UDP 고정 레이트, interval=%dms = %dHz)",
             CSI_TX_INTERVAL_MS, 1000 / CSI_TX_INTERVAL_MS);

    while (1) {
        // 게이트웨이 IP 확보 대기(접속/재접속 대응)
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        if (netif == NULL ||
            esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.gw.addr == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        dest.sin_addr.s_addr = ip_info.gw.addr;

        if (sock < 0) {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock < 0) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        }

        // 고정 레이트 송신 루프
        while (1) {
            int r = sendto(sock, payload, sizeof(payload), 0,
                           (struct sockaddr *)&dest, sizeof(dest));
            if (r < 0) break;   // 연결 끊김 등 → 바깥에서 IP/소켓 재확보
            vTaskDelay(pdMS_TO_TICKS(CSI_TX_INTERVAL_MS));
        }
        if (sock >= 0) { close(sock); sock = -1; }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

esp_err_t csi_traffic_init(void) {
    BaseType_t ok = xTaskCreate(csi_udp_sender_task, "csi_tx", 3072, NULL, 5, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_FAIL;
}