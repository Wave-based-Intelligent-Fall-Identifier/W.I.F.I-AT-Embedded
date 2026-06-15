#include "espAI.h"
#include "headers.h"
#include "originFunc.h"

void esp_ai_task(void* pvParameter) {
    // AI 코드 추후 작성
    mqtt_publish("wify/device01/AI", "DAN", 1);
    mqtt_publish("wify/device01/AI", "NOR", 1);
    mqtt_publish("wify/device01/AI", "WARN", 1);

}