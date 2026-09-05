#include "ble_recovery_policy.h"

#include <stddef.h>

#include "app_config.h"

ble_disconnect_classification_t ble_recovery_classify(
    const ble_disconnect_evidence_t *evidence)
{
    if (evidence == NULL) return BLE_DISCONNECT_NORMAL;
    if (evidence->manual) return BLE_DISCONNECT_MANUAL;
    if (evidence->connection_establishment_failure) {
        return BLE_DISCONNECT_CONNECTION_ESTABLISHMENT_FAILURE;
    }
    if (evidence->supervision_timeout || evidence->peer_dropped_recently) {
        return BLE_DISCONNECT_LIKELY_POWER_CYCLE;
    }
    if (evidence->weak_signal) return BLE_DISCONNECT_LIKELY_RF_LOSS;
    if (evidence->stable_link && evidence->platform_operational) {
        return BLE_DISCONNECT_LIKELY_POWER_CYCLE;
    }
    return BLE_DISCONNECT_NORMAL;
}

const char *ble_disconnect_classification_name(
    ble_disconnect_classification_t classification)
{
    switch (classification) {
    case BLE_DISCONNECT_NORMAL: return "NORMAL_DISCONNECT";
    case BLE_DISCONNECT_LIKELY_POWER_CYCLE: return "LIKELY_POWER_CYCLE";
    case BLE_DISCONNECT_LIKELY_RF_LOSS: return "LIKELY_RF_LOSS";
    case BLE_DISCONNECT_CONNECTION_ESTABLISHMENT_FAILURE:
        return "CONNECTION_ESTABLISHMENT_FAILURE";
    case BLE_DISCONNECT_MANUAL: return "MANUAL_DISCONNECT";
    default: return "NORMAL_DISCONNECT";
    }
}

const char *ble_fast_recovery_status_name(ble_fast_recovery_status_t status)
{
    switch (status) {
    case BLE_FAST_RECOVERY_IDLE: return "IDLE";
    case BLE_FAST_RECOVERY_ACTIVE: return "ACTIVE";
    case BLE_FAST_RECOVERY_PASS: return "PASS";
    case BLE_FAST_RECOVERY_FAILED: return "FAILED";
    default: return "IDLE";
    }
}

bool ble_recovery_classification_uses_fast_path(
    ble_disconnect_classification_t classification)
{
    return classification == BLE_DISCONNECT_LIKELY_POWER_CYCLE;
}

void ble_recovery_window_start(ble_recovery_window_t *window,
                               ble_disconnect_classification_t classification,
                               int64_t now_ms, uint32_t fast_window_ms)
{
    if (window == NULL) return;
    window->started_ms = now_ms;
    if (ble_recovery_classification_uses_fast_path(classification)) {
        window->phase = BLE_RECOVERY_PHASE_FAST;
        window->deadline_ms = now_ms + fast_window_ms;
    } else {
        window->phase = BLE_RECOVERY_PHASE_NORMAL;
        window->deadline_ms = now_ms;
    }
}

bool ble_recovery_window_is_fast(const ble_recovery_window_t *window,
                                 int64_t now_ms)
{
    return window != NULL && window->phase == BLE_RECOVERY_PHASE_FAST &&
           now_ms < window->deadline_ms;
}

uint32_t ble_recovery_retry_delay_ms(const ble_recovery_window_t *window,
                                     int64_t now_ms, uint32_t fast_retry_ms,
                                     uint32_t normal_retry_ms)
{
    return ble_recovery_window_is_fast(window, now_ms) ? fast_retry_ms : normal_retry_ms;
}

ble_recovery_scan_profile_t ble_recovery_scan_profile(bool fast)
{
    return (ble_recovery_scan_profile_t) {
        .passive = !fast,
        .filter_duplicates = !fast,
        .interval_units = fast ? APP_BLE_FAST_SCAN_INTERVAL_UNITS :
                                 APP_BLE_SCAN_INTERVAL_UNITS,
        .window_units = fast ? APP_BLE_FAST_SCAN_WINDOW_UNITS :
                               APP_BLE_SCAN_WINDOW_UNITS,
        .duration_ms = fast ? APP_BLE_FAST_RECOVERY_WINDOW_MS :
                              APP_BLE_RECOVERY_SCAN_DURATION_MS,
    };
}

uint16_t ble_supervision_timeout_response(uint16_t requested_units,
                                          uint16_t peer_units,
                                          int8_t rssi,
                                          int8_t weak_rssi_threshold)
{
    if (peer_units > requested_units && rssi <= weak_rssi_threshold) {
        return peer_units;
    }
    return requested_units;
}
