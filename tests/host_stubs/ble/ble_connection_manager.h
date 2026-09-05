#pragma once

#include <stdbool.h>
#include <stdint.h>

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
    int disconnect_classification;
    int fast_recovery_status;
} ble_connection_manager_status_t;

const char *ble_connection_state_name(ble_connection_state_t state);
