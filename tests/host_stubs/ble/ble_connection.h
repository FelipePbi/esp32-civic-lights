#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t type;
    uint8_t val[6];
} ble_addr_t;

typedef struct ble_device_entry ble_device_entry_t;

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
