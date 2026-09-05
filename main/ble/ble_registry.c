#include "ble_registry.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nimble/hci_common.h"

#define SP624E_MANUFACTURER_ID 0x5053

static ble_device_entry_t s_entries[APP_BLE_MAX_DEVICES];
static size_t s_count;
static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;

static void lock_registry(void)
{
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock_registry(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

static bool uuid_is_ffe0(const ble_uuid_t *uuid)
{
    const ble_uuid16_t ffe0 = BLE_UUID16_INIT(0xffe0);
    return ble_uuid_cmp(uuid, &ffe0.u) == 0;
}

static bool fields_advertise_ffe0(const struct ble_hs_adv_fields *fields)
{
    for (uint8_t i = 0; i < fields->num_uuids16; ++i) {
        if (uuid_is_ffe0(&fields->uuids16[i].u)) {
            return true;
        }
    }
    for (uint8_t i = 0; i < fields->num_uuids32; ++i) {
        if (uuid_is_ffe0(&fields->uuids32[i].u)) {
            return true;
        }
    }
    for (uint8_t i = 0; i < fields->num_uuids128; ++i) {
        if (uuid_is_ffe0(&fields->uuids128[i].u)) {
            return true;
        }
    }
    return false;
}

static ble_device_type_t classify(const ble_device_entry_t *entry)
{
    if (entry->manufacturer_present && entry->manufacturer_id == SP624E_MANUFACTURER_ID) {
        if (entry->manufacturer_data_len >= 2 && entry->manufacturer_data[0] == 0x0f &&
            entry->manufacturer_data[1] == 0x00) {
            return DEVICE_CONFIRMED_SP624E;
        }
        return DEVICE_POSSIBLE_BANLANX;
    }
    return entry->advertises_ffe0 ? DEVICE_GENERIC_FFE0 : DEVICE_UNKNOWN;
}

static bool manufacturer_is_confirmed_sp624e(uint16_t manufacturer_id,
                                             const uint8_t *data,
                                             uint8_t data_len)
{
    return manufacturer_id == SP624E_MANUFACTURER_ID && data_len >= 2 &&
           data[0] == 0x0f && data[1] == 0x00;
}

static bool should_replace_manufacturer(const ble_device_entry_t *entry,
                                        uint16_t incoming_id,
                                        const uint8_t *incoming_data,
                                        uint8_t incoming_len)
{
    if (!entry->manufacturer_present) {
        return true;
    }

    bool current_confirmed = manufacturer_is_confirmed_sp624e(
        entry->manufacturer_id, entry->manufacturer_data, entry->manufacturer_data_len);
    bool incoming_confirmed = manufacturer_is_confirmed_sp624e(
        incoming_id, incoming_data, incoming_len);
    if (current_confirmed != incoming_confirmed) {
        return incoming_confirmed;
    }
    if (entry->manufacturer_id == incoming_id) {
        return true;
    }
    return incoming_id == SP624E_MANUFACTURER_ID;
}

void ble_registry_init(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_count = 0;
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
}

static ble_device_entry_t *find_or_add(const ble_addr_t *address, bool *is_new)
{
    for (size_t i = 0; i < s_count; ++i) {
        if (s_entries[i].address.type == address->type &&
            memcmp(s_entries[i].address.val, address->val, sizeof(address->val)) == 0) {
            *is_new = false;
            return &s_entries[i];
        }
    }
    if (s_count >= APP_BLE_MAX_DEVICES) {
        return NULL;
    }
    ble_device_entry_t *entry = &s_entries[s_count++];
    memset(entry, 0, sizeof(*entry));
    entry->in_use = true;
    entry->address = *address;
    entry->conn_handle = BLE_CONN_HANDLE_NONE;
    entry->state = SP624E_STATE_UNKNOWN;
    *is_new = true;
    return entry;
}

static bool update_raw(uint8_t *destination, uint8_t *destination_len,
                       const uint8_t *source, uint8_t source_len)
{
    uint8_t length = source_len < BLE_REGISTRY_RAW_MAX ? source_len : BLE_REGISTRY_RAW_MAX;
    if (*destination_len == length && memcmp(destination, source, length) == 0) {
        return false;
    }
    memcpy(destination, source, length);
    *destination_len = length;
    return true;
}

ble_device_entry_t *ble_registry_update(const struct ble_gap_disc_desc *disc,
                                        const struct ble_hs_adv_fields *fields,
                                        ble_registry_update_result_t *result)
{
    memset(result, 0, sizeof(*result));
    lock_registry();
    ble_device_entry_t *entry = find_or_add(&disc->addr, &result->new_device);
    if (entry == NULL) {
        unlock_registry();
        return NULL;
    }
    entry->rssi = disc->rssi;
    entry->adv_event_type = disc->event_type;
    if (disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
        result->packet_changed = update_raw(entry->raw_scan_rsp, &entry->raw_scan_rsp_len,
                                            disc->data, disc->length_data);
    } else {
        result->packet_changed = update_raw(entry->raw_adv, &entry->raw_adv_len,
                                            disc->data, disc->length_data);
    }

    if (fields->name != NULL && fields->name_len > 0) {
        size_t length = fields->name_len < sizeof(entry->name) - 1 ?
                        fields->name_len : sizeof(entry->name) - 1;
        bool same_name = strlen(entry->name) == length &&
                         memcmp(entry->name, fields->name, length) == 0;
        bool should_update = entry->name[0] == '\0' ||
                             (!entry->name_complete && !same_name) ||
                             (!entry->name_complete && fields->name_is_complete) ||
                             (entry->name_complete && fields->name_is_complete && !same_name);
        if (should_update) {
            memcpy(entry->name, fields->name, length);
            entry->name[length] = '\0';
            entry->name_complete = fields->name_is_complete;
            result->packet_changed = true;
        }
    }
    if (fields->tx_pwr_lvl_is_present) {
        entry->tx_power_present = true;
        entry->tx_power = fields->tx_pwr_lvl;
    }
    if (fields->mfg_data != NULL && fields->mfg_data_len >= 2) {
        uint16_t incoming_id = (uint16_t)fields->mfg_data[0] |
                               ((uint16_t)fields->mfg_data[1] << 8);
        uint8_t payload_len = fields->mfg_data_len - 2;
        if (payload_len > sizeof(entry->manufacturer_data)) {
            payload_len = sizeof(entry->manufacturer_data);
        }
        if (should_replace_manufacturer(entry, incoming_id, fields->mfg_data + 2,
                                        payload_len)) {
            entry->manufacturer_present = true;
            entry->manufacturer_id = incoming_id;
            memcpy(entry->manufacturer_data, fields->mfg_data + 2, payload_len);
            entry->manufacturer_data_len = payload_len;
        }
    }
    entry->advertises_ffe0 |= fields_advertise_ffe0(fields);
    ble_device_type_t previous_type = entry->type;
    entry->type = classify(entry);
    result->classification_changed = entry->type != previous_type;
    if (entry->type == DEVICE_CONFIRMED_SP624E && entry->state == SP624E_STATE_UNKNOWN) {
        entry->state = SP624E_STATE_DISCOVERED;
    }
    unlock_registry();
    return entry;
}

size_t ble_registry_count(void)
{
    lock_registry();
    size_t count = s_count;
    unlock_registry();
    return count;
}

size_t ble_registry_confirmed_count(void)
{
    size_t count = 0;
    lock_registry();
    for (size_t i = 0; i < s_count; ++i) {
        count += s_entries[i].type == DEVICE_CONFIRMED_SP624E;
    }
    unlock_registry();
    return count;
}

ble_device_entry_t *ble_registry_get_confirmed(size_t ordinal)
{
    ble_device_entry_t *result = NULL;
    lock_registry();
    for (size_t i = 0, found = 0; i < s_count; ++i) {
        if (s_entries[i].type == DEVICE_CONFIRMED_SP624E) {
            if (found++ == ordinal) {
                result = &s_entries[i];
                break;
            }
        }
    }
    unlock_registry();
    return result;
}

ble_device_entry_t *ble_registry_ensure_mapped(const ble_addr_t *address)
{
    if (address == NULL) return NULL;
    lock_registry();
    bool is_new = false;
    ble_device_entry_t *entry = find_or_add(address, &is_new);
    if (entry != NULL) {
        /* A persisted mapping was physically provisioned and is authoritative.
         * It may be loaded before the peripheral advertises in this boot. */
        entry->type = DEVICE_CONFIRMED_SP624E;
        if (is_new) entry->state = SP624E_STATE_DISCONNECTED;
    }
    unlock_registry();
    return entry;
}

void ble_registry_format_address(const ble_addr_t *address, char *buffer, size_t size)
{
    snprintf(buffer, size, "%02X:%02X:%02X:%02X:%02X:%02X",
             address->val[5], address->val[4], address->val[3],
             address->val[2], address->val[1], address->val[0]);
}

const char *ble_registry_address_type_name(uint8_t type)
{
    switch (type) {
    case BLE_ADDR_PUBLIC: return "PUBLIC";
    case BLE_ADDR_RANDOM: return "RANDOM";
    case BLE_ADDR_PUBLIC_ID: return "PUBLIC_ID";
    case BLE_ADDR_RANDOM_ID: return "RANDOM_ID";
    default: return "UNKNOWN";
    }
}

const char *ble_registry_device_type_name(ble_device_type_t type)
{
    switch (type) {
    case DEVICE_POSSIBLE_BANLANX: return "POSSIBLE_BANLANX";
    case DEVICE_GENERIC_FFE0: return "GENERIC_FFE0_DEVICE";
    case DEVICE_CONFIRMED_SP624E: return "CONFIRMED_SP624E";
    default: return "UNKNOWN";
    }
}

const char *ble_registry_state_name(sp624e_state_t state)
{
    switch (state) {
    case SP624E_STATE_DISCOVERED: return "DISCOVERED";
    case SP624E_STATE_CONNECTING: return "CONNECTING";
    case SP624E_STATE_CONNECTED: return "CONNECTED";
    case SP624E_STATE_DISCOVERING: return "DISCOVERING";
    case SP624E_STATE_READY: return "READY";
    case SP624E_STATE_READY_FOR_CONTROL: return "READY_FOR_CONTROL";
    case SP624E_STATE_DISCONNECTED: return "DISCONNECTED";
    case SP624E_STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

void ble_registry_mark_connecting(ble_device_entry_t *entry)
{
    lock_registry(); entry->state = SP624E_STATE_CONNECTING; unlock_registry();
}

void ble_registry_mark_connected(ble_device_entry_t *entry, uint16_t conn_handle)
{
    lock_registry();
    entry->connected = true;
    entry->conn_handle = conn_handle;
    entry->connected_at_us = esp_timer_get_time();
    entry->disconnect_reason = 0;
    entry->state = SP624E_STATE_CONNECTED;
    unlock_registry();
}

void ble_registry_mark_discovering(ble_device_entry_t *entry)
{
    lock_registry(); entry->state = SP624E_STATE_DISCOVERING; unlock_registry();
}

void ble_registry_mark_gatt_result(ble_device_entry_t *entry, bool ffe0_found,
                                   bool ffe1_found, uint16_t ffe1_value_handle,
                                   uint8_t ffe1_properties)
{
    lock_registry();
    entry->service_ffe0_found = ffe0_found;
    entry->characteristic_ffe1_found = ffe1_found;
    entry->ffe1_value_handle = ffe1_value_handle;
    entry->ffe1_properties = ffe1_properties;
    if (entry->connected) {
        entry->state = entry->type == DEVICE_CONFIRMED_SP624E && ffe0_found && ffe1_found ?
                       SP624E_STATE_READY : SP624E_STATE_ERROR;
    }
    unlock_registry();
}

void ble_registry_mark_disconnected(ble_device_entry_t *entry, int reason)
{
    lock_registry();
    entry->connected = false;
    entry->disconnected_at_us = esp_timer_get_time();
    entry->disconnect_reason = reason;
    entry->conn_handle = BLE_CONN_HANDLE_NONE;
    entry->state = SP624E_STATE_DISCONNECTED;
    unlock_registry();
}

void ble_registry_mark_error(ble_device_entry_t *entry)
{
    lock_registry(); entry->state = SP624E_STATE_ERROR; unlock_registry();
}

void ble_registry_mark_ready_for_control(ble_device_entry_t *entry)
{
    lock_registry();
    if (entry->connected && entry->type == DEVICE_CONFIRMED_SP624E &&
        entry->service_ffe0_found && entry->characteristic_ffe1_found) {
        entry->state = SP624E_STATE_READY_FOR_CONTROL;
    }
    unlock_registry();
}

void ble_registry_update_rssi(ble_device_entry_t *entry, int8_t rssi)
{
    lock_registry(); entry->rssi = rssi; unlock_registry();
}

void ble_registry_get_status(const ble_device_entry_t *entry, ble_device_status_t *status)
{
    lock_registry();
    status->connected = entry->connected;
    status->conn_handle = entry->conn_handle;
    status->connected_at_us = entry->connected_at_us;
    status->disconnect_reason = entry->disconnect_reason;
    status->service_ffe0_found = entry->service_ffe0_found;
    status->characteristic_ffe1_found = entry->characteristic_ffe1_found;
    status->state = entry->state;
    status->rssi = entry->rssi;
    unlock_registry();
}
