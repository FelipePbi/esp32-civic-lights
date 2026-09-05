#include "ble_scanner.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "ble_connection.h"
#include "ble_connection_manager.h"
#include "ble_recovery_policy.h"
#include "ble_registry.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/hci_common.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "sp624e/sp624e_mapping.h"
#include "sp624e/sp624e_provisioning.h"

static const char *TAG_SCAN = "BLE_SCAN";
static const char *TAG_REGISTRY = "BLE_REGISTRY";
static const char *TAG_SP624E = "SP624E";
static uint8_t s_own_addr_type;
static bool s_scan_active;
static bool s_target_mode;
static bool s_initial_scan_complete;
static ble_addr_t s_target_addresses[SP624E_SIDE_COUNT];
static size_t s_target_count;
static bool s_fast_recovery_scan;

static bool address_equal(const ble_addr_t *left, const ble_addr_t *right)
{
    return left != NULL && right != NULL && left->type == right->type &&
           memcmp(left->val, right->val, sizeof(left->val)) == 0;
}

static const char *event_type_name(uint8_t type)
{
    switch (type) {
    case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND: return "ADV_IND";
    case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND: return "ADV_DIRECT_IND";
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND: return "ADV_SCAN_IND";
    case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND: return "ADV_NONCONN_IND";
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP: return "SCAN_RESPONSE";
    default: return "UNKNOWN";
    }
}

static void bytes_to_hex(const uint8_t *data, size_t length, char *output, size_t size)
{
    size_t used = 0;
    output[0] = '\0';
    for (size_t i = 0; i < length && used + 3 < size; ++i) {
        used += (size_t)snprintf(output + used, size - used, "%02X%s", data[i],
                                i + 1 < length ? " " : "");
    }
}

static void log_service_uuids(const struct ble_hs_adv_fields *fields)
{
    char uuid[BLE_UUID_STR_LEN];
    if (fields->num_uuids16 == 0 && fields->num_uuids32 == 0 && fields->num_uuids128 == 0) {
        ESP_LOGI(TAG_SCAN, "Services: <none in this packet>");
        return;
    }
    for (uint8_t i = 0; i < fields->num_uuids16; ++i) {
        ESP_LOGI(TAG_SCAN, "Service UUID16: %s",
                 ble_uuid_to_str(&fields->uuids16[i].u, uuid));
    }
    for (uint8_t i = 0; i < fields->num_uuids32; ++i) {
        ESP_LOGI(TAG_SCAN, "Service UUID32: %s",
                 ble_uuid_to_str(&fields->uuids32[i].u, uuid));
    }
    for (uint8_t i = 0; i < fields->num_uuids128; ++i) {
        ESP_LOGI(TAG_SCAN, "Service UUID128: %s",
                 ble_uuid_to_str(&fields->uuids128[i].u, uuid));
    }
}

static void log_service_data(const struct ble_hs_adv_fields *fields)
{
    char hex[(BLE_HS_ADV_MAX_FIELD_SZ * 3) + 1];
    if (fields->svc_data_uuid16_len > 0) {
        bytes_to_hex(fields->svc_data_uuid16, fields->svc_data_uuid16_len, hex, sizeof(hex));
        ESP_LOGI(TAG_SCAN, "Service Data UUID16: %s", hex);
    }
    if (fields->svc_data_uuid32_len > 0) {
        bytes_to_hex(fields->svc_data_uuid32, fields->svc_data_uuid32_len, hex, sizeof(hex));
        ESP_LOGI(TAG_SCAN, "Service Data UUID32: %s", hex);
    }
    if (fields->svc_data_uuid128_len > 0) {
        bytes_to_hex(fields->svc_data_uuid128, fields->svc_data_uuid128_len, hex, sizeof(hex));
        ESP_LOGI(TAG_SCAN, "Service Data UUID128: %s", hex);
    }
}

static void log_packet(const struct ble_gap_disc_desc *disc,
                       const struct ble_hs_adv_fields *fields,
                       const ble_device_entry_t *entry)
{
    char address[18];
    char raw[(BLE_HS_ADV_MAX_SZ * 3) + 1];
    ble_registry_format_address(&disc->addr, address, sizeof(address));
    bytes_to_hex(disc->data, disc->length_data, raw, sizeof(raw));
    ESP_LOGI(TAG_SCAN, "----------------------------------------");
    ESP_LOGI(TAG_SCAN, "BLE DEVICE packet=%s", event_type_name(disc->event_type));
    ESP_LOGI(TAG_SCAN, "Address: %s", address);
    ESP_LOGI(TAG_SCAN, "Address type: %s", ble_registry_address_type_name(disc->addr.type));
    ESP_LOGI(TAG_SCAN, "RSSI: %d dBm", disc->rssi);
    if (fields->name != NULL && fields->name_len > 0) {
        ESP_LOGI(TAG_SCAN, "%s local name: %.*s",
                 fields->name_is_complete ? "Complete" : "Short",
                 fields->name_len, (const char *)fields->name);
    } else {
        ESP_LOGI(TAG_SCAN, "Local name: <none in this packet>");
    }
    ESP_LOGI(TAG_SCAN, "Flags: 0x%02X", fields->flags);
    if (fields->tx_pwr_lvl_is_present) {
        ESP_LOGI(TAG_SCAN, "TX power: %d dBm", fields->tx_pwr_lvl);
    }
    if (fields->mfg_data != NULL && fields->mfg_data_len >= 2) {
        uint16_t manufacturer_id = (uint16_t)fields->mfg_data[0] |
                                   ((uint16_t)fields->mfg_data[1] << 8);
        char manufacturer_hex[(BLE_HS_ADV_MAX_FIELD_SZ * 3) + 1];
        bytes_to_hex(fields->mfg_data + 2, fields->mfg_data_len - 2,
                     manufacturer_hex, sizeof(manufacturer_hex));
        ESP_LOGI(TAG_SCAN, "Manufacturer ID: 0x%04X (little-endian decoded)",
                 manufacturer_id);
        ESP_LOGI(TAG_SCAN, "Manufacturer payload: %s", manufacturer_hex);
    } else if (fields->mfg_data != NULL) {
        char malformed[(BLE_HS_ADV_MAX_FIELD_SZ * 3) + 1];
        bytes_to_hex(fields->mfg_data, fields->mfg_data_len, malformed, sizeof(malformed));
        ESP_LOGW(TAG_SCAN, "Manufacturer data malformed (missing 16-bit ID): %s", malformed);
    } else {
        ESP_LOGI(TAG_SCAN, "Manufacturer: <none in this packet>");
    }
    log_service_uuids(fields);
    log_service_data(fields);
    ESP_LOGI(TAG_SCAN, "Raw %s: %s", disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP ?
             "SCAN RESPONSE" : "ADV", raw);
    ESP_LOGI(TAG_SCAN, "Registry classification: %s",
             ble_registry_device_type_name(entry->type));
    ESP_LOGI(TAG_SCAN, "----------------------------------------");
}

static void log_confirmed_entry(size_t ordinal, const ble_device_entry_t *entry)
{
    char address[18];
    char manufacturer[(BLE_REGISTRY_MFG_DATA_MAX * 3) + 1];
    char raw_adv[(BLE_REGISTRY_RAW_MAX * 3) + 1];
    char raw_scan_rsp[(BLE_REGISTRY_RAW_MAX * 3) + 1];
    ble_registry_format_address(&entry->address, address, sizeof(address));
    bytes_to_hex(entry->manufacturer_data, entry->manufacturer_data_len,
                 manufacturer, sizeof(manufacturer));
    bytes_to_hex(entry->raw_adv, entry->raw_adv_len, raw_adv, sizeof(raw_adv));
    bytes_to_hex(entry->raw_scan_rsp, entry->raw_scan_rsp_len,
                 raw_scan_rsp, sizeof(raw_scan_rsp));
    ESP_LOGI(TAG_SP624E, "SP624E #%u", (unsigned)(ordinal + 1));
    ESP_LOGI(TAG_SP624E, "Address: %s", address);
    ESP_LOGI(TAG_SP624E, "Address type: %s",
             ble_registry_address_type_name(entry->address.type));
    ESP_LOGI(TAG_SP624E, "RSSI: %d dBm", entry->rssi);
    ESP_LOGI(TAG_SP624E, "Name: %s", entry->name[0] != '\0' ? entry->name : "<none>");
    ESP_LOGI(TAG_SP624E, "Manufacturer ID: 0x%04X", entry->manufacturer_id);
    ESP_LOGI(TAG_SP624E, "Manufacturer Data: %s", manufacturer);
    ESP_LOGI(TAG_SP624E, "Raw ADV: %s", raw_adv);
    ESP_LOGI(TAG_SP624E, "Raw Scan Response: %s",
             entry->raw_scan_rsp_len > 0 ? raw_scan_rsp : "<none>");
}

static void scan_complete(int reason)
{
    s_scan_active = false;
    if (s_initial_scan_complete) {
        ESP_LOGI(TAG_SCAN, "%s recovery scan completed reason=%d",
                 s_fast_recovery_scan ? "FAST" : "NORMAL", reason);
        s_target_mode = false;
        s_target_count = 0;
        s_fast_recovery_scan = false;
        return;
    }
    s_initial_scan_complete = true;
    size_t total = ble_registry_count();
    size_t confirmed = ble_registry_confirmed_count();
    ESP_LOGI(TAG_SCAN, "=======================================");
    ESP_LOGI(TAG_SCAN, "BLE SCAN SUMMARY");
    ESP_LOGI(TAG_SCAN, "Total unique devices: %u", (unsigned)total);
    ESP_LOGI(TAG_SCAN, "Confirmed SP624E: %u", (unsigned)confirmed);
    ESP_LOGI(TAG_SCAN, "Scan completion reason: %d", reason);
    for (size_t i = 0; i < confirmed; ++i) {
        log_confirmed_entry(i, ble_registry_get_confirmed(i));
    }
    ESP_LOGI(TAG_SCAN, "=======================================");
    sp624e_mapping_t mapping;
    if (sp624e_mapping_load(&mapping) == ESP_OK && mapping.valid) {
        ble_addr_t left = {.type = mapping.left.type};
        ble_addr_t right = {.type = mapping.right.type};
        memcpy(left.val, mapping.left.val, sizeof(left.val));
        memcpy(right.val, mapping.right.val, sizeof(right.val));
        ble_device_entry_t *left_entry = ble_registry_ensure_mapped(&left);
        ble_device_entry_t *right_entry = ble_registry_ensure_mapped(&right);
        char left_text[18], right_text[18];
        ble_registry_format_address(&left, left_text, sizeof(left_text));
        ble_registry_format_address(&right, right_text, sizeof(right_text));
        ESP_LOGI(TAG_SP624E,
                 "Persisted mapping loaded LEFT=%s RIGHT=%s; missing devices will use targeted recovery scans",
                 left_text, right_text);
        if (left_entry != NULL && right_entry != NULL) {
            ble_connection_start_two(s_own_addr_type, left_entry, right_entry);
        } else {
            ESP_LOGE(TAG_SP624E, "Cannot reserve registry entries for persisted mapping");
        }
        return;
    }
    if (confirmed == 2) {
        ble_connection_start_two(s_own_addr_type,
                                 ble_registry_get_confirmed(0),
                                 ble_registry_get_confirmed(1));
    } else if (confirmed < 2) {
        ESP_LOGW(TAG_SP624E,
                 "Need exactly two confirmed SP624E; found %u. No random connection attempted.",
                 (unsigned)confirmed);
    } else {
        ESP_LOGW(TAG_SP624E,
                 "More than two confirmed SP624E found (%u). Provisioning required; no RSSI-based selection.",
                 (unsigned)confirmed);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_hs_adv_fields fields = {0};
        int rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                         event->disc.length_data);
        if (rc != 0) {
            char address[18];
            ble_registry_format_address(&event->disc.addr, address, sizeof(address));
            ESP_LOGW(TAG_SCAN, "Advertisement parse failed address=%s rc=%d", address, rc);
            return 0;
        }
        ble_registry_update_result_t result;
        ble_device_entry_t *entry = ble_registry_update(&event->disc, &fields, &result);
        if (entry == NULL) {
            ESP_LOGW(TAG_REGISTRY, "Registry full; advertisement dropped");
            return 0;
        }
        if (result.packet_changed || result.new_device) {
            log_packet(&event->disc, &fields, entry);
        }
        if (result.classification_changed && entry->type == DEVICE_CONFIRMED_SP624E) {
            char address[18];
            ble_registry_format_address(&entry->address, address, sizeof(address));
            ESP_LOGI(TAG_SP624E,
                     "Confirmed SP624E address=%s manufacturer=0x%04X prefix=%02X %02X",
                     address, entry->manufacturer_id, entry->manufacturer_data[0],
                     entry->manufacturer_data[1]);
        }
        if (s_target_mode) {
            for (size_t i = 0; i < s_target_count; ++i) {
                if (address_equal(&event->disc.addr, &s_target_addresses[i])) {
                    ble_connection_manager_on_advertisement(&event->disc.addr,
                                                            event->disc.rssi);
                    break;
                }
            }
        }
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        scan_complete(event->disc_complete.reason);
    }
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG_SCAN, "No BLE identity address: rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG_SCAN, "Cannot infer BLE address type: rc=%d", rc);
        return;
    }
    sp624e_mapping_t mapping;
    if (sp624e_mapping_load(&mapping) == ESP_OK && mapping.valid) {
        ble_addr_t left = {.type = mapping.left.type};
        ble_addr_t right = {.type = mapping.right.type};
        memcpy(left.val, mapping.left.val, sizeof(left.val));
        memcpy(right.val, mapping.right.val, sizeof(right.val));
        ble_device_entry_t *left_entry = ble_registry_ensure_mapped(&left);
        ble_device_entry_t *right_entry = ble_registry_ensure_mapped(&right);
        if (left_entry != NULL && right_entry != NULL) {
            s_initial_scan_complete = true;
            ESP_LOGI(TAG_SCAN,
                     "Persisted mapping available; starting targeted connection recovery immediately");
            ble_connection_start_two(s_own_addr_type, left_entry, right_entry);
            return;
        }
        ESP_LOGE(TAG_SCAN, "Cannot reserve registry entries for persisted mapping");
    }
    struct ble_gap_disc_params params = {0};
    params.passive = 0;
    params.filter_duplicates = 0;
    params.itvl = APP_BLE_SCAN_INTERVAL_UNITS;
    params.window = APP_BLE_SCAN_WINDOW_UNITS;
    params.filter_policy = 0;
    params.limited = 0;
    ESP_LOGI(TAG_SCAN, "NimBLE initialized. Active scan for %d ms; registry merging enabled.",
             APP_BLE_SCAN_DURATION_MS);
    rc = ble_gap_disc(s_own_addr_type, APP_BLE_SCAN_DURATION_MS, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG_SCAN, "BLE scan start failed: rc=%d", rc);
    } else {
        s_scan_active = true;
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG_SCAN, "NimBLE host reset: reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG_SCAN, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_scanner_start(void)
{
    ble_registry_init();
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG_SCAN, "NimBLE init failed: %s", esp_err_to_name(err));
        return err;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

int ble_scanner_start_target(const ble_addr_t *address)
{
    return ble_scanner_start_targets(address, address != NULL ? 1 : 0, false);
}

int ble_scanner_start_targets(const ble_addr_t *addresses, size_t count, bool fast)
{
    if (addresses == NULL || count == 0 || count > SP624E_SIDE_COUNT) {
        return BLE_HS_EINVAL;
    }
    if (s_scan_active) return BLE_HS_EALREADY;
    s_target_mode = true;
    s_fast_recovery_scan = fast;
    s_target_count = count;
    memcpy(s_target_addresses, addresses, count * sizeof(addresses[0]));
    ble_recovery_scan_profile_t profile = ble_recovery_scan_profile(fast);
    struct ble_gap_disc_params params = {0};
    params.passive = profile.passive;
    params.filter_duplicates = profile.filter_duplicates;
    params.itvl = profile.interval_units;
    params.window = profile.window_units;
    params.filter_policy = 0;
    params.limited = 0;
    char first[18];
    char second[18] = "<none>";
    ble_registry_format_address(&addresses[0], first, sizeof(first));
    if (count > 1) ble_registry_format_address(&addresses[1], second, sizeof(second));
    ESP_LOGI(TAG_SCAN,
             "%s recovery scan targets=%s,%s active=%d duplicate_filter=%d duty=%u/%u",
             fast ? "FAST" : "NORMAL", first, second, !params.passive,
             params.filter_duplicates, params.window, params.itvl);
    int rc = ble_gap_disc(s_own_addr_type, profile.duration_ms,
                          &params, gap_event, NULL);
    if (rc == 0) {
        s_scan_active = true;
    } else {
        s_target_mode = false;
        s_target_count = 0;
        s_fast_recovery_scan = false;
    }
    return rc;
}

int ble_scanner_stop_scan(void)
{
    if (!s_scan_active) return 0;
    int rc = ble_gap_disc_cancel();
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        s_scan_active = false;
        s_target_mode = false;
        s_target_count = 0;
        s_fast_recovery_scan = false;
        return 0;
    }
    return rc;
}

bool ble_scanner_is_active(void) { return s_scan_active; }
