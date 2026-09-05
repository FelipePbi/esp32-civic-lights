#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "indicator_policy.h"

typedef struct {
    bool initialized;
    bool on;
    indicator_reason_t reason;
    int gpio_level;
    int64_t heartbeat_ms;
    int64_t last_change_ms;
} indicator_snapshot_t;

esp_err_t indicator_init(void);
void indicator_get_snapshot(indicator_snapshot_t *snapshot);
bool indicator_run_self_test(void);
