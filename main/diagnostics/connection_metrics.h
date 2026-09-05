#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sync/group_types.h"
#include "ble/ble_recovery_policy.h"

typedef struct {
    uint32_t connect_attempts;
    uint32_t successful_connections;
    uint32_t disconnect_count;
    uint32_t reconnect_success_count;
    uint32_t reconnect_failure_count;
    int last_disconnect_reason;
    int64_t last_disconnect_timestamp_ms;
    int64_t last_connect_timestamp_ms;
    uint64_t total_connected_time_ms;
    uint32_t current_backoff_ms;
    int8_t last_rssi;
    uint32_t state_query_count;
    uint32_t state_query_failures;
    uint32_t reconcile_count;
    uint32_t reconcile_failures;
    uint32_t command_count;
    uint32_t command_failures;
    uint32_t stale_commands_discarded;
    uint32_t queue_depth;
    uint32_t max_queue_depth;
    uint32_t coalesced_commands;
    uint32_t forced_recoveries;
    uint32_t critical_event_replacements;
    uint32_t power_cycle_suspected_count;
    uint32_t fast_recovery_count;
    uint32_t fast_recovery_success;
    uint32_t fast_recovery_failure;
    uint32_t avg_disconnect_detection_ms;
    uint32_t max_disconnect_detection_ms;
    uint32_t avg_adv_to_connect_ms;
    uint32_t max_adv_to_connect_ms;
    uint32_t avg_adv_to_ready_ms;
    uint32_t max_adv_to_ready_ms;
    uint32_t avg_disconnect_to_synced_ms;
    uint32_t max_disconnect_to_synced_ms;
    uint32_t connection_0x3e_count;
    uint32_t supervision_timeout_disconnects;
    uint32_t dual_disconnect_count;
    uint32_t gatt_fast_path_attempts;
    uint32_t gatt_fast_path_success;
    uint32_t gatt_fast_path_fallbacks;
    ble_disconnect_classification_t last_disconnect_classification;
    ble_fast_recovery_status_t fast_recovery_status;
    int64_t last_ble_rx_ms;
    int64_t last_state_query_valid_ms;
    int64_t recovery_started_ms;
    int64_t scan_started_ms;
    int64_t first_adv_ms;
    int64_t connect_started_ms;
    int64_t connected_ms;
    int64_t gatt_ready_ms;
    int64_t cccd_subscribed_ms;
    int64_t state_valid_ms;
    int64_t reconcile_started_ms;
    int64_t group_synced_ms;
    uint32_t last_adv_after_loss_ms;
    uint32_t last_recovery_duration_ms;
    uint16_t requested_supervision_timeout_ms;
    uint16_t accepted_supervision_timeout_ms;
} sp624e_connection_metrics_t;

typedef struct {
    uint32_t group_sync_count;
    uint32_t group_desync_count;
    uint32_t group_degraded_count;
    int64_t last_sync_time_ms;
    int64_t desync_started_ms;
    uint64_t max_desync_duration_ms;
    uint32_t desired_generation;
    uint32_t verified_generation[SP624E_SIDE_COUNT];
    uint32_t dual_disconnect_count;
} sp624e_group_metrics_t;

void sp624e_metrics_init(void);
void sp624e_metrics_get_side(sp624e_side_t side, sp624e_connection_metrics_t *metrics);
void sp624e_metrics_set_side(sp624e_side_t side, const sp624e_connection_metrics_t *metrics);
void sp624e_metrics_get_group(sp624e_group_metrics_t *metrics);
void sp624e_metrics_set_group(const sp624e_group_metrics_t *metrics);
