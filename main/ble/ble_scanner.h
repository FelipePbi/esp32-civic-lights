#pragma once

#include "esp_err.h"
#include "host/ble_hs.h"

#include <stddef.h>

esp_err_t ble_scanner_start(void);
int ble_scanner_start_target(const ble_addr_t *address);
int ble_scanner_start_targets(const ble_addr_t *addresses, size_t count, bool fast);
int ble_scanner_stop_scan(void);
bool ble_scanner_is_active(void);
