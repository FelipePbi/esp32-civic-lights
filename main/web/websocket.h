#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdint.h>

esp_err_t websocket_register(httpd_handle_t server);
void websocket_start_events(httpd_handle_t server);
void websocket_publish(const char *type);
int64_t websocket_heartbeat_ms(void);
uint32_t websocket_event_drop_count(void);
