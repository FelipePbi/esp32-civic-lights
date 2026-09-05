#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host/ble_hs.h"

#define BLE_REGISTRY_NAME_MAX 48
#define BLE_REGISTRY_MFG_DATA_MAX 29
#define BLE_REGISTRY_RAW_MAX BLE_HS_ADV_MAX_SZ
#define BLE_CONN_HANDLE_NONE 0xffff

typedef enum {
    DEVICE_UNKNOWN = 0,
    DEVICE_POSSIBLE_BANLANX,
    DEVICE_GENERIC_FFE0,
    DEVICE_CONFIRMED_SP624E,
} ble_device_type_t;

typedef enum {
    SP624E_STATE_UNKNOWN = 0,
    SP624E_STATE_DISCOVERED,
    SP624E_STATE_CONNECTING,
    SP624E_STATE_CONNECTED,
    SP624E_STATE_DISCOVERING,
    SP624E_STATE_READY,
    SP624E_STATE_READY_FOR_CONTROL,
    SP624E_STATE_DISCONNECTED,
    SP624E_STATE_ERROR,
} sp624e_state_t;

typedef struct {
    bool in_use;
    ble_addr_t address;
    int8_t rssi;
    char name[BLE_REGISTRY_NAME_MAX];
    bool name_complete;
    bool tx_power_present;
    int8_t tx_power;
    uint16_t manufacturer_id;
    bool manufacturer_present;
    uint8_t manufacturer_data[BLE_REGISTRY_MFG_DATA_MAX];
    uint8_t manufacturer_data_len;
    bool advertises_ffe0;
    ble_device_type_t type;
    uint8_t adv_event_type;
    uint8_t raw_adv[BLE_REGISTRY_RAW_MAX];
    uint8_t raw_adv_len;
    uint8_t raw_scan_rsp[BLE_REGISTRY_RAW_MAX];
    uint8_t raw_scan_rsp_len;
    bool connected;
    uint16_t conn_handle;
    int64_t connected_at_us;
    int64_t disconnected_at_us;
    int disconnect_reason;
    bool service_ffe0_found;
    bool characteristic_ffe1_found;
    uint16_t ffe1_value_handle;
    uint8_t ffe1_properties;
    sp624e_state_t state;
} ble_device_entry_t;

typedef struct {
    bool new_device;
    bool packet_changed;
    bool classification_changed;
} ble_registry_update_result_t;

typedef struct {
    bool connected;
    uint16_t conn_handle;
    int64_t connected_at_us;
    int disconnect_reason;
    bool service_ffe0_found;
    bool characteristic_ffe1_found;
    sp624e_state_t state;
    int8_t rssi;
} ble_device_status_t;

void ble_registry_init(void);
ble_device_entry_t *ble_registry_update(const struct ble_gap_disc_desc *disc,
                                        const struct ble_hs_adv_fields *fields,
                                        ble_registry_update_result_t *result);
size_t ble_registry_count(void);
size_t ble_registry_confirmed_count(void);
ble_device_entry_t *ble_registry_get_confirmed(size_t ordinal);
ble_device_entry_t *ble_registry_ensure_mapped(const ble_addr_t *address);
void ble_registry_format_address(const ble_addr_t *address, char *buffer, size_t size);
const char *ble_registry_address_type_name(uint8_t type);
const char *ble_registry_device_type_name(ble_device_type_t type);
const char *ble_registry_state_name(sp624e_state_t state);
void ble_registry_mark_connecting(ble_device_entry_t *entry);
void ble_registry_mark_connected(ble_device_entry_t *entry, uint16_t conn_handle);
void ble_registry_mark_discovering(ble_device_entry_t *entry);
void ble_registry_mark_gatt_result(ble_device_entry_t *entry, bool ffe0_found,
                                   bool ffe1_found, uint16_t ffe1_value_handle,
                                   uint8_t ffe1_properties);
void ble_registry_mark_disconnected(ble_device_entry_t *entry, int reason);
void ble_registry_mark_error(ble_device_entry_t *entry);
void ble_registry_mark_ready_for_control(ble_device_entry_t *entry);
void ble_registry_update_rssi(ble_device_entry_t *entry, int8_t rssi);
void ble_registry_get_status(const ble_device_entry_t *entry, ble_device_status_t *status);
