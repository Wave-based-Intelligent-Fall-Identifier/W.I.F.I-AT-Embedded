#include "espnowAP.h"

const static char* TAG = "ESP-NOW-AP";
static EventGroupHandle_t wifiEventGroup;

static void wifiHandler(void *args, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
    if (eventId == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "WiFi AP모드 시작");
    }
    else if (eventId == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) eventData;
        ESP_LOGI(TAG, "장치 접속됨 MAC: " MACSTR ", AID: %d", MAC2STR(event->mac), event->aid);    
    }
    else if (eventId == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) eventData;
        ESP_LOGI(TAG, "장치 연결 끊김 MAC: " MACSTR ", AID: %d", MAC2STR(event->mac), event->aid);
    }
}

esp_err_t wifiInit(void) {
    esp_err_t err;
    wifiEventGroup = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

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

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = 6,
            .password = WIFI_PASS,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi 초기화 성공, 연결 대기 중");
    EventBits_t bits = xEventGroupWaitBits(wifiEventGroup, GOT_IP_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & GOT_IP_BIT) {
        ESP_LOGI(TAG, "WiFi 연결 성공, IP획득");
        return ESP_OK;
    } 
    else if (bits & FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi 연결 실패");
        return ESP_FAIL;
    } 
    else {
        ESP_LOGE(TAG, "예상치 못한 에러 발생");
        return ESP_FAIL;
    }

    return ESP_OK;
}