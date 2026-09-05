#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t wifi_ap_start(void);
uint8_t wifi_ap_client_count(void);
