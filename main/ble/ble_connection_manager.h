#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "ble_connection.h"
#include "diagnostics/connection_metrics.h"
#include "sync/group_types.h"

typedef enum {
    BLE_CONNECTION_UNKNOWN = 0,
    BLE_CONNECTION_DISCONNECTED,
    BLE_CONNECTION_WAITING_FOR_ADV,
    BLE_CONNECTION_CONNECTING,
    BLE_CONNECTION_CONNECTED,
    BLE_CONNECTION_DISCOVERING,
    BLE_CONNECTION_SUBSCRIBING,
    BLE_CONNECTION_QUERYING_STATE,
    BLE_CONNECTION_SYNC_PENDING,
    BLE_CONNECTION_RECONCILING,
    BLE_CONNECTION_READY,
    BLE_CONNECTION_BACKOFF,
    BLE_CONNECTION_FAST_RECOVERY,
    BLE_CONNECTION_RECOVERING,
    BLE_CONNECTION_ERROR,
} ble_connection_state_t;

typedef struct {
    ble_connection_state_t state;
    bool connected;
    bool gatt_ready;
    uint16_t conn_handle;
    int8_t rssi;
    uint32_t reconnect_attempt;
    uint32_t current_backoff_ms;
    int64_t state_entered_ms;
    int64_t ready_since_ms;
    ble_disconnect_classification_t disconnect_classification;
    ble_fast_recovery_status_t fast_recovery_status;
} ble_connection_manager_status_t;

/*
 * Optional guards invoked right before the manager needs the BLE master role
 * (recovery scan or connect). Secondary BLE users register here so SP624E
 * recovery always preempts them: the diagnostic console and the interior
 * light. Guards run on the manager task and must return quickly.
 */
#define BLE_MASTER_GUARD_MAX 2
typedef void (*ble_master_guard_fn)(const char *reason);
esp_err_t ble_connection_manager_add_master_guard(ble_master_guard_fn guard);

void ble_connection_manager_start(void);
void ble_connection_manager_on_connected(sp624e_side_t side, uint16_t conn_handle);
void ble_connection_manager_on_connect_failed(sp624e_side_t side, int reason);
void ble_connection_manager_on_gatt_ready(sp624e_side_t side);
void ble_connection_manager_on_gatt_failed(sp624e_side_t side, int reason);
void ble_connection_manager_on_disconnected(sp624e_side_t side, int reason);
void ble_connection_manager_on_advertisement(const ble_addr_t *address, int8_t rssi);
void ble_connection_manager_on_ble_rx(sp624e_side_t side, int64_t received_ms);
void ble_connection_manager_on_valid_state(sp624e_side_t side);
void ble_connection_manager_on_group_synced(void);
void ble_connection_manager_on_connection_params(sp624e_side_t side,
                                                 uint16_t requested_ms,
                                                 uint16_t accepted_ms);
void ble_connection_manager_on_fast_gatt_failed(sp624e_side_t side, int reason);
void ble_connection_manager_set_stage(sp624e_side_t side,
                                      ble_connection_state_t state,
                                      const char *reason);
void ble_connection_manager_mark_unhealthy(sp624e_side_t side, const char *reason);
void ble_connection_manager_request_disconnect(sp624e_side_t side);
void ble_connection_manager_request_power_cycle_test(sp624e_side_t side);
void ble_connection_manager_request_reconnect(sp624e_side_t side);
bool ble_connection_manager_is_ready(sp624e_side_t side);
bool ble_connection_manager_both_ready(void);
bool ble_connection_manager_is_sync_eligible(sp624e_side_t side);
bool ble_connection_manager_both_sync_eligible(void);
bool ble_connection_manager_any_fast_recovery(void);
void ble_connection_manager_get_status(sp624e_side_t side,
                                       ble_connection_manager_status_t *status);
const char *ble_connection_state_name(ble_connection_state_t state);
int64_t ble_connection_manager_heartbeat_ms(void);
