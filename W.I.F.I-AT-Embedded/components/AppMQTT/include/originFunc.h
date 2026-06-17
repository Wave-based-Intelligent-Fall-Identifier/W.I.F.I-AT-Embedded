#ifndef APP_BLE_ORIGIN_FUNC
#define APP_BLE_ORIGIN_FUNC

#include "headers.h"

/**
 * @brief mqtt5 초기화 및 핸들러 등록 함수
 * @param void* pvParameters
 * @return void
 */
void mqtt5_init(void* pvParameters);

/**
 * @brief 전역에서 MQTT를 통해 메시지 전달 함수
 * @param
 *  - topic : broker에게 알릴 전송 주소
 *  - data  : 전송 데이터
 *  - qos   : 전송 품질 (1 권장)
 */
esp_err_t mqtt_publish(const char* topic, const char* data, int qos, int max_retry);

esp_err_t mqtt_wait_connected(uint32_t timeout_ms);


/** 
 * @brief 기기 현재 연결 상태 전송 함수
 * @param void* pvParameter
 * @return void
 */
void heartbeat_task(void* pvParameter);

/**
 * @brief 네트워크 현재 상태 전송 함수
 */
void network_status(void);

/** 
 * @brief 현재 네트워크 id/passwd 전송 함수
 */
void network_settings(void);

/**
 * @brief 수신 데이터 처리 함수
 */
void Server_dataa_process(int topic_len, char* topic, int data_len, char* data);

/**
 * @brief 네트워크 id/passwd 재입력 요구 메시지 전송 함수
 */
void network_settings_send_again(void);

#endif