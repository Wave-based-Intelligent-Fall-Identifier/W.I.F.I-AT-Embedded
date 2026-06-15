#include "espAI.h"
#include "headers.h"
#include "originFunc.h"

void esp_ai_task(void* pvParameter) {
    // AI 파트 개발 후 작성 예정
    while (0) {
        mqtt_publish("wify/device01/AI", "DAN", 1, 1);
        mqtt_publish("wify/device01/AI", "NOR", 1, 1);
        mqtt_publish("wify/device01/AI", "WARN", 1, 1);
    }
        
    vTaskDelete(NULL);
}