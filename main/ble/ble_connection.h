#pragma once

#include <stdint.h>

#include "ble_registry.h"
#include "sync/group_types.h"

typedef struct {
    ble_device_entry_t *entry;
    uint16_t conn_handle;
    uint16_t ffe1_handle;
    uint16_t cccd_handle;
    bool signature_confirmed;
    bool ffe0_found;
    bool ffe1_found;
    bool cccd_found;
    bool cached_handles;
} sp624e_transport_t;

void ble_connection_start_two(uint8_t own_addr_type, ble_device_entry_t *first,
                              ble_device_entry_t *second);
int ble_connection_connect(sp624e_side_t side, uint32_t timeout_ms);
int ble_connection_cancel_connect(sp624e_side_t side);
int ble_connection_terminate(sp624e_side_t side, uint8_t reason);
int ble_connection_start_full_discovery(sp624e_side_t side);
void ble_connection_force_cleanup(sp624e_side_t side, int reason);
bool ble_connection_get_transport(sp624e_side_t side, sp624e_transport_t *transport);
const ble_addr_t *ble_connection_get_address(sp624e_side_t side);
int8_t ble_connection_get_rssi(sp624e_side_t side);
bool ble_connection_was_observed(sp624e_side_t side);
