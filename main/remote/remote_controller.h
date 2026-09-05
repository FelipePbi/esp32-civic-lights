#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rf_config.h"
#include "rf_remote.h"

typedef struct {
    bool initialized;
    bool has_last_button;
    remote_button_t last_button;
    rf_physical_channel_t last_channel;
    uint64_t last_event_ms;
    bool last_action_accepted;
    uint32_t event_drops;
    rf_remote_snapshot_t receiver;
    rf_remote_config_t config;
} remote_controller_snapshot_t;

typedef void (*remote_controller_event_fn)(const char *event);

esp_err_t remote_controller_init(remote_controller_event_fn event_fn);
void remote_controller_get_snapshot(remote_controller_snapshot_t *snapshot);
