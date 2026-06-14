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
esp_err_t mqtt_publish(const char* topic, const char* data, int qos);

#endif