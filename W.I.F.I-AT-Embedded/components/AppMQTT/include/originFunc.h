#ifndef APP_BLE_ORIGIN_FUNC
#define APP_BLE_ORIGIN_FUNC

#include "headers.h"

static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void log_error_if_nonzero(const char *message, int error_code);
static void print_user_property(mqtt5_user_property_handle_t user_property);

#endif