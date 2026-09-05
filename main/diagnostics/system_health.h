#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool healthy;
    int reset_reason;
    char reset_reason_name[24];
    char previous_recovery_reason[32];
    char stale_component[24];
    uint32_t supervisor_restart_count;
    uint32_t free_heap;
    uint32_t minimum_free_heap;
    int64_t connection_manager_heartbeat_ms;
    int64_t group_runtime_heartbeat_ms;
    int64_t rf_heartbeat_ms;
    int64_t indicator_heartbeat_ms;
    int64_t web_heartbeat_ms;
    uint32_t ble_forced_recoveries;
    uint32_t ble_critical_event_replacements;
    uint32_t group_api_timeouts;
    uint32_t group_api_busy;
    uint32_t group_api_response_drops;
    uint32_t rf_event_drops;
    uint32_t websocket_event_drops;
    int indicator_gpio_level;
    int64_t indicator_last_change_ms;
} system_health_snapshot_t;

esp_err_t system_health_init(void);
void system_health_get_snapshot(system_health_snapshot_t *snapshot);
