#include <stdio.h>
#include "esp_log.h"
#include "pir-sensor.h"
#include "espnowAP.h"
#include "gpio_definitions.h"

const static char* TAG = "Main";

void app_main(void) {
    ESP_LOGI(TAG, "[step 1] Initializing System...");
    ESP_ERROR_CHECK(wifiInit());

    ESP_LOGI(TAG, "[step 2] Initializing Peripherals...");
    gpio_pin_init();


    ESP_LOGI(TAG, "[step 3] Initializing ESP-NOW...");
    xTaskCreate(espnow_csi_send, "espnow_csi_send", 4096, NULL, 5, NULL);
    xTaskCreate(pir_sensor, "pir_sensor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "[step 4] Starting system!");
}