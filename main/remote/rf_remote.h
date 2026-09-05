#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rf_config.h"

typedef struct {
    bool initialized;
    bool vt_active;
    bool mapping_complete;
    bool discovery_active;
    bool has_last_channel;
    rf_physical_channel_t last_channel;
    uint64_t last_event_ms;
    int64_t heartbeat_ms;
} rf_remote_snapshot_t;

typedef void (*rf_remote_event_fn)(remote_button_t button,
                                   rf_physical_channel_t channel,
                                   bool vt_active);

esp_err_t rf_remote_init(rf_remote_event_fn event_fn);
void rf_remote_get_snapshot(rf_remote_snapshot_t *snapshot);
bool rf_remote_set_discovery(bool enabled);
