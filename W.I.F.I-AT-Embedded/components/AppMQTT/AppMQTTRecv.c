#include "headers.h"
#include "originFunc.h"
#include "common_struct.h"

static const char *TAG = "MQTT Recv";

void Server_dataa_process(int topic_len, char* topic, int data_len, char* data) {
    static char new_ssid[33] = {0};
    static char new_passwd[65] = {0};

    if (topic_len == strlen(WIFY_TOPIC("/baseline/cmd")) && strncmp(topic, WIFY_TOPIC("/baseline/cmd"), topic_len) == 0) {
        // 앱 계약(WIFY_MQTT_APP_PROTOCOL §4): payload = {"cmd":"BASELINE_REBUILD"} (JSON).
        // 정확한 JSON 파싱 대신 명령 토큰 존재만 확인(구 평문 "BASELINEREBUILD" 도 허용).
        char buf[64];
        int len = (data_len < (int)sizeof(buf) - 1) ? data_len : (int)sizeof(buf) - 1;
        memcpy(buf, data, len);
        buf[len] = '\0';
        if (strstr(buf, "BASELINE_REBUILD") != NULL || strstr(buf, "BASELINEREBUILD") != NULL) {
            g_baseline_reset_req = true;
            ESP_LOGI(TAG, "baseline 재설정 명령 수신, 다음 CSI 프레임에 재탐지");
        }
        else {
            ESP_LOGW(TAG, "Topic과 Data가 매칭되지 않음, Notion MQTT Topic Table 참고");
        }
    }

    else if (topic_len == strlen(WIFY_TOPIC("/edit/nownetwork")) && strncmp(topic, WIFY_TOPIC("/edit/nownetwork"), topic_len) == 0) {
        ESP_LOGI(TAG, "새로운 id/passwd 입력");

        char buf[128];
        int len = (data_len < (int)sizeof(buf) - 1) ? data_len : (int)sizeof(buf) - 1;
        memcpy(buf, data, len);
        buf[len] = '\0';

        const char* id_start = strstr(buf, "id:");
        const char* pw_start = strstr(buf, "passwd:");

        if (id_start && pw_start) {
            id_start += strlen("id:");
            pw_start += strlen("passwd:");

            int id_len = strcspn(id_start, " ");
            int pw_len = strcspn(pw_start, " ");

            if (id_len >= (int)sizeof(new_ssid)) {
                id_len = sizeof(new_ssid) - 1;
            }

            if (pw_len >= (int)sizeof(new_passwd)) {
                pw_len = sizeof(new_passwd) - 1;
            }

            strncpy(new_ssid, id_start, id_len);
            new_ssid[id_len] = '\0';
            strncpy(new_passwd, pw_start, pw_len);
            new_passwd[pw_len] = '\0';

            ESP_LOGI(TAG, "파싱 결과 ssid=%s, passwd=%s", new_ssid, new_passwd);
        } else {
            ESP_LOGE(TAG, "id/passwd 형식 오류");
            network_settings_send_again();
            return;
        }
    }

    else if (topic_len == strlen(WIFY_TOPIC("/edit/editnetwork")) && strncmp(topic, WIFY_TOPIC("/edit/editnetwork"), topic_len) == 0) {
        if (data_len == (int)strlen("NEWNETWORKEDIT") && strncmp(data, "NEWNETWORKEDIT", data_len) == 0) {
            ESP_LOGI(TAG, "새로운 id/passwd 적용");

            if (strlen(new_ssid) == 0) {
                ESP_LOGW(TAG, "저장된 ssid 없음, 먼저 nownetwork로 입력 필요");
                return;
            }

            esp_wifi_disconnect();
            wifi_config_t wifi_config = {0};
            strncpy((char*)wifi_config.sta.ssid, new_ssid, sizeof(wifi_config.sta.ssid) - 1);
            strncpy((char*)wifi_config.sta.password, new_passwd, sizeof(wifi_config.sta.password) - 1);
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

            esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "새 Wi-Fi 설정 실패");
                return;
            }

            err = esp_wifi_connect();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "재연결 시도 실패");
                return;
            }

            ESP_LOGI(TAG, "새 Wi-Fi로 재연결 시도 중...");
        }

        else {
            ESP_LOGW(TAG, "예상되지 않은 Topic 수신");
        }
    }

    else {
        ESP_LOGW(TAG, "Topic과 Data가 매칭되지 않음, Notion MQTT Topic Table 참고");
    }
}