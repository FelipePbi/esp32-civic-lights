#pragma once

#include <stdint.h>

uint32_t ble_backoff_base_ms(uint32_t attempt);
uint32_t ble_backoff_with_jitter(uint32_t base_ms, int jitter_percent);
uint32_t ble_backoff_absent_retry_ms(void);
