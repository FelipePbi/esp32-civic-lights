#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BLE_DISCONNECT_NORMAL = 0,
    BLE_DISCONNECT_LIKELY_POWER_CYCLE,
    BLE_DISCONNECT_LIKELY_RF_LOSS,
    BLE_DISCONNECT_CONNECTION_ESTABLISHMENT_FAILURE,
    BLE_DISCONNECT_MANUAL,
} ble_disconnect_classification_t;

typedef enum {
    BLE_RECOVERY_PHASE_NORMAL = 0,
    BLE_RECOVERY_PHASE_FAST,
} ble_recovery_phase_t;

typedef enum {
    BLE_FAST_RECOVERY_IDLE = 0,
    BLE_FAST_RECOVERY_ACTIVE,
    BLE_FAST_RECOVERY_PASS,
    BLE_FAST_RECOVERY_FAILED,
} ble_fast_recovery_status_t;

typedef struct {
    bool manual;
    bool supervision_timeout;
    bool connection_establishment_failure;
    bool stable_link;
    bool platform_operational;
    bool peer_dropped_recently;
    bool weak_signal;
} ble_disconnect_evidence_t;

typedef struct {
    ble_recovery_phase_t phase;
    int64_t started_ms;
    int64_t deadline_ms;
} ble_recovery_window_t;

typedef struct {
    bool passive;
    bool filter_duplicates;
    uint16_t interval_units;
    uint16_t window_units;
    uint32_t duration_ms;
} ble_recovery_scan_profile_t;

ble_disconnect_classification_t ble_recovery_classify(
    const ble_disconnect_evidence_t *evidence);
const char *ble_disconnect_classification_name(
    ble_disconnect_classification_t classification);
const char *ble_fast_recovery_status_name(ble_fast_recovery_status_t status);
bool ble_recovery_classification_uses_fast_path(
    ble_disconnect_classification_t classification);
void ble_recovery_window_start(ble_recovery_window_t *window,
                               ble_disconnect_classification_t classification,
                               int64_t now_ms, uint32_t fast_window_ms);
bool ble_recovery_window_is_fast(const ble_recovery_window_t *window,
                                 int64_t now_ms);
uint32_t ble_recovery_retry_delay_ms(const ble_recovery_window_t *window,
                                     int64_t now_ms, uint32_t fast_retry_ms,
                                     uint32_t normal_retry_ms);
ble_recovery_scan_profile_t ble_recovery_scan_profile(bool fast);
uint16_t ble_supervision_timeout_response(uint16_t requested_units,
                                          uint16_t peer_units,
                                          int8_t rssi,
                                          int8_t weak_rssi_threshold);
