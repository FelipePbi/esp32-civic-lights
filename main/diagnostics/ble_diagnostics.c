#include "ble_diagnostics.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "ble/ble_connection_manager.h"
#include "ble_diag_format.h"
#include "console/runtime_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "os/os_mbuf.h"
#include "sdkconfig.h"
#include "sync/group_types.h"

_Static_assert(BLE_DIAG_PROP_READ == BLE_GATT_CHR_PROP_READ, "property bit drift");
_Static_assert(BLE_DIAG_PROP_WRITE == BLE_GATT_CHR_PROP_WRITE, "property bit drift");
_Static_assert(BLE_DIAG_PROP_WRITE_NO_RSP == BLE_GATT_CHR_PROP_WRITE_NO_RSP,
               "property bit drift");
_Static_assert(BLE_DIAG_PROP_NOTIFY == BLE_GATT_CHR_PROP_NOTIFY, "property bit drift");
_Static_assert(BLE_DIAG_PROP_INDICATE == BLE_GATT_CHR_PROP_INDICATE, "property bit drift");

#define TAG "BLE-DIAG"

#define DIAG_ADDRESS_TEXT_MAX 18
#define DIAG_ERROR_TEXT_MAX 64
#define DIAG_NAME_MAX 32
#define DIAG_RAW_MAX BLE_HS_ADV_MAX_SZ
#define DIAG_MAX_SUBSCRIPTIONS 8
#define DIAG_HEX_TEXT(bytes) ((bytes) * 3 + 1)

#define DIAG_LOG(level, section, format, ...)                                             \
    do {                                                                                  \
        char diag_stamp_text[32];                                                         \
        diag_stamp(diag_stamp_text, sizeof(diag_stamp_text));                             \
        ESP_LOG##level(TAG, "[%s][" section "] " format, diag_stamp_text, ##__VA_ARGS__);  \
    } while (0)

#define DIAG_INFO(section, format, ...) DIAG_LOG(I, section, format, ##__VA_ARGS__)
#define DIAG_WARN(section, format, ...) DIAG_LOG(W, section, format, ##__VA_ARGS__)
#define DIAG_FAIL(format, ...) DIAG_LOG(E, "ERROR", format, ##__VA_ARGS__)

typedef enum {
    DIAG_MASTER_IDLE = 0,
    DIAG_MASTER_SCANNING,
    DIAG_MASTER_CONNECTING,
} diag_master_op_t;

typedef enum {
    DIAG_LINK_DISCONNECTED = 0,
    DIAG_LINK_CONNECTING,
    DIAG_LINK_CONNECTED,
} diag_link_state_t;

typedef enum {
    DIAG_DISCOVERY_NONE = 0,
    DIAG_DISCOVERY_RUNNING,
    DIAG_DISCOVERY_DONE,
    DIAG_DISCOVERY_FAILED,
} diag_discovery_state_t;

typedef struct {
    bool in_use;
    ble_addr_t address;
    int8_t rssi_last;
    int8_t rssi_best;
    uint8_t adv_event_type;
    bool name_complete;
    uint16_t adv_packets;
    uint16_t rsp_packets;
    char name[DIAG_NAME_MAX];
    uint8_t raw_adv[DIAG_RAW_MAX];
    uint8_t raw_adv_len;
    uint8_t raw_scan_rsp[DIAG_RAW_MAX];
    uint8_t raw_scan_rsp_len;
    int64_t first_seen_ms;
    int64_t last_seen_ms;
} diag_device_t;

typedef struct {
    struct ble_gatt_svc service;
} diag_service_t;

typedef struct {
    struct ble_gatt_chr characteristic;
    uint8_t service_index;
} diag_characteristic_t;

typedef struct {
    struct ble_gatt_dsc descriptor;
    uint16_t characteristic_value_handle;
} diag_descriptor_t;

typedef struct {
    bool in_use;      /* slot reserved: CCCD write issued or confirmed */
    bool active;      /* CCCD write confirmed by the peripheral */
    bool subscribing; /* direction of the CCCD write in flight */
    uint16_t value_handle;
    uint16_t cccd_handle;
    bool indicate;
    ble_uuid_any_t uuid;
} diag_subscription_t;

static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_storage;
static bool s_initialized;
static bool s_enabled;

/* Read without the lock on the connection manager fast path. */
static volatile diag_master_op_t s_master_op;
static uint32_t s_scan_requested_ms;
static int64_t s_scan_started_ms;
static uint32_t s_scan_packets;
static uint32_t s_scan_sessions;
static bool s_scan_table_full;

/* Scan and GATT tables live on the heap only while diagnostics is enabled:
   the disabled firmware must not spend a byte of the tight runtime heap. */
static diag_device_t *s_devices;

static bool s_target_valid;
static ble_addr_t s_target;

static diag_link_state_t s_link_state;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_mtu;
static int s_last_disconnect_reason;

static diag_discovery_state_t s_discovery_state;
static diag_service_t *s_services;
static diag_characteristic_t *s_characteristics;
static diag_descriptor_t *s_descriptors;
static uint8_t s_service_count;
static uint8_t s_characteristic_count;
static uint8_t s_descriptor_count;
static uint8_t s_walk_service;
static uint8_t s_walk_characteristic;

static diag_subscription_t s_subscriptions[DIAG_MAX_SUBSCRIPTIONS];

static char s_last_error[DIAG_ERROR_TEXT_MAX] = "none";

static int diag_gap_event(struct ble_gap_event *event, void *arg);
static void diag_start_service_discovery(void);
static void diag_walk_characteristics(void);
static void diag_walk_descriptors(void);
static void diag_finish_discovery(int status);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static int64_t diag_now_ms(void) { return esp_timer_get_time() / 1000; }

static void diag_stamp(char *out, size_t size)
{
    int64_t ms = diag_now_ms();
    snprintf(out, size, "%" PRId64 ".%03" PRId64, ms / 1000, ms % 1000);
}

static void diag_lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void diag_unlock(void) { xSemaphoreGive(s_lock); }

/* Caller holds the diagnostic lock. The four tables are allocated and released
   together, so one pointer answers for all of them. */
static bool diag_tables_ready(void) { return s_devices != NULL; }

/* Must be called without the diagnostic lock held. */
static void diag_set_error(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void diag_set_error(const char *format, ...)
{
    char text[DIAG_ERROR_TEXT_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    diag_lock();
    snprintf(s_last_error, sizeof(s_last_error), "%s", text);
    diag_unlock();
    DIAG_FAIL("%s", text);
}

static void diag_format_address(const ble_addr_t *address, char *out, size_t size)
{
    if (address == NULL) {
        snprintf(out, size, "<none>");
        return;
    }
    ble_diag_address_format(address->val, out, size);
}

static const char *diag_address_type_name(uint8_t type)
{
    switch (type) {
    case BLE_ADDR_PUBLIC: return "public";
    case BLE_ADDR_RANDOM: return "random";
    case BLE_ADDR_PUBLIC_ID: return "public-identity";
    case BLE_ADDR_RANDOM_ID: return "random-identity";
    default: return "unknown";
    }
}

static const char *diag_adv_event_name(uint8_t type)
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

static bool diag_address_equal(const ble_addr_t *left, const ble_addr_t *right)
{
    return left->type == right->type && memcmp(left->val, right->val, 6) == 0;
}

static bool diag_ble_ready(void)
{
    return ble_hs_is_enabled() != 0 && ble_hs_synced() != 0;
}

static int diag_own_addr_type(uint8_t *out)
{
    return ble_hs_id_infer_auto(0, out);
}

/* ------------------------------------------------------------------ */
/* Scan session                                                        */
/* ------------------------------------------------------------------ */

static diag_device_t *diag_device_find(const ble_addr_t *address)
{
    if (!diag_tables_ready()) return NULL;
    for (size_t i = 0; i < APP_BLE_DIAG_MAX_DEVICES; ++i) {
        if (s_devices[i].in_use && diag_address_equal(&s_devices[i].address, address)) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static diag_device_t *diag_device_reserve(const ble_addr_t *address)
{
    if (!diag_tables_ready()) return NULL;
    for (size_t i = 0; i < APP_BLE_DIAG_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            diag_device_t *device = &s_devices[i];
            memset(device, 0, sizeof(*device));
            device->in_use = true;
            device->address = *address;
            device->rssi_best = INT8_MIN;
            device->first_seen_ms = diag_now_ms();
            return device;
        }
    }
    return NULL;
}

static size_t diag_device_count(void)
{
    size_t count = 0;
    if (!diag_tables_ready()) return 0;
    for (size_t i = 0; i < APP_BLE_DIAG_MAX_DEVICES; ++i) {
        if (s_devices[i].in_use) count++;
    }
    return count;
}

static void diag_log_adv_fields(const struct ble_hs_adv_fields *fields)
{
    char uuid[BLE_UUID_STR_LEN];
    char hex[DIAG_HEX_TEXT(BLE_HS_ADV_MAX_FIELD_SZ)];
    for (uint8_t i = 0; i < fields->num_uuids16; ++i) {
        DIAG_INFO("SCAN", "  Service UUID16: %s", ble_uuid_to_str(&fields->uuids16[i].u, uuid));
    }
    for (uint8_t i = 0; i < fields->num_uuids32; ++i) {
        DIAG_INFO("SCAN", "  Service UUID32: %s", ble_uuid_to_str(&fields->uuids32[i].u, uuid));
    }
    for (uint8_t i = 0; i < fields->num_uuids128; ++i) {
        DIAG_INFO("SCAN", "  Service UUID128: %s",
                  ble_uuid_to_str(&fields->uuids128[i].u, uuid));
    }
    if (fields->num_uuids16 == 0 && fields->num_uuids32 == 0 && fields->num_uuids128 == 0) {
        DIAG_INFO("SCAN", "  Services: <none in this packet>");
    }
    if (fields->mfg_data != NULL && fields->mfg_data_len >= 2) {
        uint16_t identifier = (uint16_t)fields->mfg_data[0] |
                              ((uint16_t)fields->mfg_data[1] << 8);
        ble_diag_hex_encode(fields->mfg_data + 2, (size_t)(fields->mfg_data_len - 2), hex,
                            sizeof(hex));
        DIAG_INFO("SCAN", "  Manufacturer ID: 0x%04X (little-endian)", identifier);
        DIAG_INFO("SCAN", "  Manufacturer payload: %s", hex);
    } else if (fields->mfg_data != NULL) {
        ble_diag_hex_encode(fields->mfg_data, fields->mfg_data_len, hex, sizeof(hex));
        DIAG_WARN("SCAN", "  Manufacturer data without 16-bit ID: %s", hex);
    } else {
        DIAG_INFO("SCAN", "  Manufacturer: <none in this packet>");
    }
    if (fields->svc_data_uuid16_len > 0) {
        ble_diag_hex_encode(fields->svc_data_uuid16, fields->svc_data_uuid16_len, hex,
                            sizeof(hex));
        DIAG_INFO("SCAN", "  Service Data UUID16: %s", hex);
    }
    if (fields->svc_data_uuid32_len > 0) {
        ble_diag_hex_encode(fields->svc_data_uuid32, fields->svc_data_uuid32_len, hex,
                            sizeof(hex));
        DIAG_INFO("SCAN", "  Service Data UUID32: %s", hex);
    }
    if (fields->svc_data_uuid128_len > 0) {
        ble_diag_hex_encode(fields->svc_data_uuid128, fields->svc_data_uuid128_len, hex,
                            sizeof(hex));
        DIAG_INFO("SCAN", "  Service Data UUID128: %s", hex);
    }
    if (fields->tx_pwr_lvl_is_present) {
        DIAG_INFO("SCAN", "  TX power: %d dBm", fields->tx_pwr_lvl);
    }
    if (fields->appearance_is_present) {
        DIAG_INFO("SCAN", "  Appearance: 0x%04X", fields->appearance);
    }
    DIAG_INFO("SCAN", "  Flags: 0x%02X", fields->flags);
}

static void diag_log_device_detail(const diag_device_t *device, const char *heading)
{
    char address[DIAG_ADDRESS_TEXT_MAX];
    char hex[DIAG_HEX_TEXT(DIAG_RAW_MAX)];
    struct ble_hs_adv_fields fields;
    diag_format_address(&device->address, address, sizeof(address));
    DIAG_INFO("SCAN", "----------------------------------------");
    DIAG_INFO("SCAN", "%s", heading);
    DIAG_INFO("SCAN", "Name: %s%s", device->name[0] != '\0' ? device->name : "<none>",
              device->name[0] != '\0' && !device->name_complete ? " (short)" : "");
    DIAG_INFO("SCAN", "Address: %s", address);
    DIAG_INFO("SCAN", "Address type: %s", diag_address_type_name(device->address.type));
    DIAG_INFO("SCAN", "RSSI: %d dBm (best %d dBm)", device->rssi_last, device->rssi_best);
    DIAG_INFO("SCAN", "Packet type: %s", diag_adv_event_name(device->adv_event_type));
    if (device->raw_adv_len > 0 &&
        ble_hs_adv_parse_fields(&fields, device->raw_adv, device->raw_adv_len) == 0) {
        DIAG_INFO("SCAN", "Advertisement fields:");
        diag_log_adv_fields(&fields);
    }
    if (device->raw_scan_rsp_len > 0 &&
        ble_hs_adv_parse_fields(&fields, device->raw_scan_rsp, device->raw_scan_rsp_len) == 0) {
        DIAG_INFO("SCAN", "Scan response fields:");
        diag_log_adv_fields(&fields);
    }
    ble_diag_hex_encode(device->raw_adv, device->raw_adv_len, hex, sizeof(hex));
    DIAG_INFO("SCAN", "Advertisement RAW: %s", device->raw_adv_len > 0 ? hex : "<none>");
    ble_diag_hex_encode(device->raw_scan_rsp, device->raw_scan_rsp_len, hex, sizeof(hex));
    DIAG_INFO("SCAN", "Scan response RAW: %s", device->raw_scan_rsp_len > 0 ? hex : "<none>");
    DIAG_INFO("SCAN", "----------------------------------------");
}

static void diag_scan_report(const struct ble_gap_disc_desc *disc,
                             const struct ble_hs_adv_fields *fields)
{
    bool is_response = disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
    uint8_t length = disc->length_data > DIAG_RAW_MAX ? DIAG_RAW_MAX : disc->length_data;
    bool detail = false;
    bool changed = false;
    diag_device_t snapshot;

    diag_lock();
    s_scan_packets++;
    diag_device_t *device = diag_device_find(&disc->addr);
    if (device == NULL) {
        device = diag_device_reserve(&disc->addr);
        if (device == NULL) {
            bool first = !s_scan_table_full;
            s_scan_table_full = true;
            diag_unlock();
            if (first) {
                DIAG_WARN("SCAN", "Device table full (%d entries); use 'diag scan clear'",
                          APP_BLE_DIAG_MAX_DEVICES);
            }
            return;
        }
        detail = true;
    }
    device->last_seen_ms = diag_now_ms();
    device->rssi_last = disc->rssi;
    if (disc->rssi > device->rssi_best) device->rssi_best = disc->rssi;
    if (!is_response) device->adv_event_type = disc->event_type;

    uint8_t *slot = is_response ? device->raw_scan_rsp : device->raw_adv;
    uint8_t *slot_len = is_response ? &device->raw_scan_rsp_len : &device->raw_adv_len;
    if (*slot_len != length || memcmp(slot, disc->data, length) != 0) {
        memcpy(slot, disc->data, length);
        *slot_len = length;
        changed = true;
    }
    if (is_response) {
        device->rsp_packets++;
    } else {
        device->adv_packets++;
    }
    if (fields->name != NULL && fields->name_len > 0 &&
        (device->name[0] == '\0' || (fields->name_is_complete && !device->name_complete))) {
        size_t copy = fields->name_len < DIAG_NAME_MAX - 1 ? fields->name_len
                                                          : DIAG_NAME_MAX - 1;
        memcpy(device->name, fields->name, copy);
        device->name[copy] = '\0';
        device->name_complete = fields->name_is_complete;
        changed = true;
    }
    snapshot = *device;
    diag_unlock();

    /* Dedupe: full block on first sighting, one line when the payload changes. */
    if (detail) {
        diag_log_device_detail(&snapshot, "NEW DEVICE");
        return;
    }
    if (changed) {
        char address[DIAG_ADDRESS_TEXT_MAX];
        diag_format_address(&snapshot.address, address, sizeof(address));
        DIAG_INFO("SCAN", "Updated %s packet=%s name=%s rssi=%d", address,
                  diag_adv_event_name(disc->event_type),
                  snapshot.name[0] != '\0' ? snapshot.name : "<none>", disc->rssi);
    }
}

static void diag_scan_finished(const char *reason)
{
    uint32_t packets;
    uint32_t elapsed;
    size_t devices;
    diag_lock();
    if (s_master_op == DIAG_MASTER_SCANNING) s_master_op = DIAG_MASTER_IDLE;
    packets = s_scan_packets;
    elapsed = (uint32_t)(diag_now_ms() - s_scan_started_ms);
    devices = diag_device_count();
    diag_unlock();
    DIAG_INFO("SCAN", "Scan finished reason=%s elapsed=%" PRIu32 "ms packets=%" PRIu32
              " unique_devices=%u", reason, elapsed, packets, (unsigned)devices);
    DIAG_INFO("SCAN", "Use 'diag scan list' to review, 'diag scan clear' to reset the table");
}

/* ------------------------------------------------------------------ */
/* GATT discovery                                                      */
/* ------------------------------------------------------------------ */

static void diag_uuid_string(const ble_uuid_t *uuid, char out[BLE_UUID_STR_LEN])
{
    ble_uuid_to_str(uuid, out);
}

static int diag_service_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                                   const struct ble_gatt_svc *service, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0 && service != NULL) {
        diag_lock();
        bool stored = diag_tables_ready() && s_service_count < APP_BLE_MAX_SERVICES;
        if (stored) s_services[s_service_count++].service = *service;
        diag_unlock();
        if (!stored) {
            char uuid[BLE_UUID_STR_LEN];
            diag_uuid_string(&service->uuid.u, uuid);
            DIAG_WARN("GATT", "Service table full; dropped %s", uuid);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        diag_lock();
        s_walk_service = 0;
        diag_unlock();
        diag_walk_characteristics();
        return 0;
    }
    diag_finish_discovery(error->status);
    return 0;
}

static int diag_characteristic_discovered(uint16_t conn_handle,
                                          const struct ble_gatt_error *error,
                                          const struct ble_gatt_chr *characteristic,
                                          void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0 && characteristic != NULL) {
        diag_lock();
        bool stored = diag_tables_ready() &&
                      s_characteristic_count < APP_BLE_MAX_CHARACTERISTICS;
        if (stored) {
            diag_characteristic_t *record = &s_characteristics[s_characteristic_count++];
            record->characteristic = *characteristic;
            record->service_index = s_walk_service;
        }
        diag_unlock();
        if (!stored) {
            char uuid[BLE_UUID_STR_LEN];
            diag_uuid_string(&characteristic->uuid.u, uuid);
            DIAG_WARN("GATT", "Characteristic table full; dropped %s", uuid);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        diag_lock();
        s_walk_service++;
        diag_unlock();
        diag_walk_characteristics();
        return 0;
    }
    diag_finish_discovery(error->status);
    return 0;
}

static int diag_descriptor_discovered(uint16_t conn_handle,
                                      const struct ble_gatt_error *error,
                                      uint16_t characteristic_value_handle,
                                      const struct ble_gatt_dsc *descriptor, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0 && descriptor != NULL) {
        diag_lock();
        bool stored = diag_tables_ready() && s_descriptor_count < APP_BLE_MAX_DESCRIPTORS;
        if (stored) {
            diag_descriptor_t *record = &s_descriptors[s_descriptor_count++];
            record->descriptor = *descriptor;
            record->characteristic_value_handle = characteristic_value_handle;
        }
        diag_unlock();
        if (!stored) {
            char uuid[BLE_UUID_STR_LEN];
            diag_uuid_string(&descriptor->uuid.u, uuid);
            DIAG_WARN("GATT", "Descriptor table full; dropped %s", uuid);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        diag_lock();
        s_walk_characteristic++;
        diag_unlock();
        diag_walk_descriptors();
        return 0;
    }
    diag_finish_discovery(error->status);
    return 0;
}

static void diag_start_service_discovery(void)
{
    uint16_t conn_handle;
    diag_lock();
    if (!diag_tables_ready()) {
        diag_unlock();
        diag_set_error("diagnostic tables released; run 'diag enable' first");
        return;
    }
    memset(s_services, 0, APP_BLE_MAX_SERVICES * sizeof(*s_services));
    memset(s_characteristics, 0, APP_BLE_MAX_CHARACTERISTICS * sizeof(*s_characteristics));
    memset(s_descriptors, 0, APP_BLE_MAX_DESCRIPTORS * sizeof(*s_descriptors));
    s_service_count = 0;
    s_characteristic_count = 0;
    s_descriptor_count = 0;
    s_walk_service = 0;
    s_walk_characteristic = 0;
    s_discovery_state = DIAG_DISCOVERY_RUNNING;
    conn_handle = s_conn_handle;
    diag_unlock();
    DIAG_INFO("GATT", "Full discovery started conn=%u", conn_handle);
    int rc = ble_gattc_disc_all_svcs(conn_handle, diag_service_discovered, NULL);
    if (rc != 0) diag_finish_discovery(rc);
}

static void diag_walk_characteristics(void)
{
    struct ble_gatt_svc service;
    uint16_t conn_handle;
    diag_lock();
    bool done = s_walk_service >= s_service_count;
    if (!done) service = s_services[s_walk_service].service;
    conn_handle = s_conn_handle;
    if (done) s_walk_characteristic = 0;
    diag_unlock();
    if (done) {
        diag_walk_descriptors();
        return;
    }
    int rc = ble_gattc_disc_all_chrs(conn_handle, service.start_handle, service.end_handle,
                                     diag_characteristic_discovered, NULL);
    if (rc != 0) diag_finish_discovery(rc);
}

/* Caller holds the diagnostic lock. */
static uint16_t diag_descriptor_end_handle(uint8_t index)
{
    const diag_characteristic_t *current = &s_characteristics[index];
    for (uint8_t next = index + 1; next < s_characteristic_count; ++next) {
        if (s_characteristics[next].service_index == current->service_index) {
            return (uint16_t)(s_characteristics[next].characteristic.def_handle - 1);
        }
    }
    return s_services[current->service_index].service.end_handle;
}

static void diag_walk_descriptors(void)
{
    while (true) {
        uint16_t conn_handle;
        uint16_t value_handle = 0;
        uint16_t end_handle = 0;
        bool done;
        diag_lock();
        done = s_walk_characteristic >= s_characteristic_count;
        if (!done) {
            value_handle =
                s_characteristics[s_walk_characteristic].characteristic.val_handle;
            end_handle = diag_descriptor_end_handle(s_walk_characteristic);
        }
        conn_handle = s_conn_handle;
        diag_unlock();
        if (done) {
            diag_finish_discovery(0);
            return;
        }
        if (end_handle > value_handle) {
            int rc = ble_gattc_disc_all_dscs(conn_handle, value_handle, end_handle,
                                             diag_descriptor_discovered, NULL);
            if (rc == 0) return;
            DIAG_WARN("GATT", "Descriptor discovery start failed value=0x%04X rc=%d",
                      value_handle, rc);
        }
        diag_lock();
        s_walk_characteristic++;
        diag_unlock();
    }
}

static void diag_dump_profile(void)
{
    uint8_t services, characteristics, descriptors;
    diag_lock();
    services = s_service_count;
    characteristics = s_characteristic_count;
    descriptors = s_descriptor_count;
    diag_unlock();

    DIAG_INFO("GATT", "========================================");
    DIAG_INFO("GATT", "GATT PROFILE services=%u characteristics=%u descriptors=%u", services,
              characteristics, descriptors);
    for (uint8_t i = 0; i < services; ++i) {
        diag_service_t service;
        diag_lock();
        service = s_services[i];
        diag_unlock();
        char uuid[BLE_UUID_STR_LEN];
        diag_uuid_string(&service.service.uuid.u, uuid);
        DIAG_INFO("GATT", "SERVICE %s handles=0x%04X-0x%04X", uuid,
                  service.service.start_handle, service.service.end_handle);
        for (uint8_t j = 0; j < characteristics; ++j) {
            diag_characteristic_t record;
            diag_lock();
            record = s_characteristics[j];
            diag_unlock();
            if (record.service_index != i) continue;
            char chr_uuid[BLE_UUID_STR_LEN];
            char properties[96];
            diag_uuid_string(&record.characteristic.uuid.u, chr_uuid);
            ble_diag_properties_string(record.characteristic.properties, properties,
                                       sizeof(properties));
            DIAG_INFO("GATT",
                      "  CHARACTERISTIC %s def=0x%04X value=0x%04X properties=0x%02X [%s]",
                      chr_uuid, record.characteristic.def_handle,
                      record.characteristic.val_handle, record.characteristic.properties,
                      properties);
            for (uint8_t k = 0; k < descriptors; ++k) {
                diag_descriptor_t descriptor;
                diag_lock();
                descriptor = s_descriptors[k];
                diag_unlock();
                if (descriptor.characteristic_value_handle !=
                    record.characteristic.val_handle) {
                    continue;
                }
                char dsc_uuid[BLE_UUID_STR_LEN];
                diag_uuid_string(&descriptor.descriptor.uuid.u, dsc_uuid);
                DIAG_INFO("GATT", "    DESCRIPTOR %s handle=0x%04X", dsc_uuid,
                          descriptor.descriptor.handle);
            }
        }
    }
    DIAG_INFO("GATT", "----------------------------------------");
    DIAG_INFO("GATT", "Candidates for reverse engineering (WRITE/NOTIFY/INDICATE):");
    bool any = false;
    for (uint8_t j = 0; j < characteristics; ++j) {
        diag_characteristic_t record;
        diag_service_t service;
        diag_lock();
        record = s_characteristics[j];
        service = s_services[record.service_index];
        diag_unlock();
        if (!ble_diag_properties_are_interesting(record.characteristic.properties)) continue;
        char chr_uuid[BLE_UUID_STR_LEN];
        char svc_uuid[BLE_UUID_STR_LEN];
        char properties[96];
        diag_uuid_string(&record.characteristic.uuid.u, chr_uuid);
        diag_uuid_string(&service.service.uuid.u, svc_uuid);
        ble_diag_properties_string(record.characteristic.properties, properties,
                                   sizeof(properties));
        DIAG_INFO("GATT", "  %s / %s [%s]", svc_uuid, chr_uuid, properties);
        any = true;
    }
    if (!any) DIAG_INFO("GATT", "  <none>");
    DIAG_INFO("GATT", "Properties alone do not identify a colour channel; confirm by "
                      "observing the real controller.");
    DIAG_INFO("GATT", "========================================");
}

static void diag_finish_discovery(int status)
{
    bool report;
    diag_lock();
    report = s_discovery_state == DIAG_DISCOVERY_RUNNING;
    if (report) {
        s_discovery_state = status == 0 ? DIAG_DISCOVERY_DONE : DIAG_DISCOVERY_FAILED;
    }
    diag_unlock();
    if (!report) return;
    if (status != 0) {
        diag_set_error("GATT discovery failed status=%d", status);
        return;
    }
    diag_dump_profile();
}

/* ------------------------------------------------------------------ */
/* GATT operations                                                     */
/* ------------------------------------------------------------------ */

static int diag_read_result(uint16_t conn_handle, const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status != 0) {
        diag_set_error("READ failed handle=0x%04X status=%d", error->att_handle,
                       error->status);
        return 0;
    }
    uint8_t data[APP_BLE_DIAG_PAYLOAD_MAX_BYTES];
    uint16_t length = attr != NULL && attr->om != NULL ? OS_MBUF_PKTLEN(attr->om) : 0;
    uint16_t copied = length > sizeof(data) ? (uint16_t)sizeof(data) : length;
    if (copied > 0 && os_mbuf_copydata(attr->om, 0, copied, data) != 0) {
        diag_set_error("READ mbuf copy failed handle=0x%04X", attr->handle);
        return 0;
    }
    char hex[DIAG_HEX_TEXT(APP_BLE_DIAG_PAYLOAD_MAX_BYTES)];
    char ascii[APP_BLE_DIAG_PAYLOAD_MAX_BYTES + 1];
    ble_diag_hex_encode(data, copied, hex, sizeof(hex));
    ble_diag_ascii_encode(data, copied, ascii, sizeof(ascii));
    DIAG_INFO("READ", "handle=0x%04X length=%u%s", attr != NULL ? attr->handle : 0, length,
              copied < length ? " (truncated in log)" : "");
    DIAG_INFO("READ", "HEX: %s", hex);
    DIAG_INFO("READ", "ASCII: %s", ascii);
    return 0;
}

static int diag_write_result(uint16_t conn_handle, const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status != 0) {
        diag_set_error("WRITE failed handle=0x%04X status=%d", error->att_handle,
                       error->status);
        return 0;
    }
    DIAG_INFO("WRITE", "Acknowledged handle=0x%04X", attr != NULL ? attr->handle : 0);
    return 0;
}

static int diag_cccd_result(uint16_t conn_handle, const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    size_t index = (size_t)(uintptr_t)arg;
    if (index >= DIAG_MAX_SUBSCRIPTIONS) return 0;
    char uuid[BLE_UUID_STR_LEN];
    bool subscribing;
    uint16_t value_handle;
    diag_lock();
    subscribing = s_subscriptions[index].subscribing;
    value_handle = s_subscriptions[index].value_handle;
    diag_uuid_string(&s_subscriptions[index].uuid.u, uuid);
    if (error->status != 0) {
        /* A failed subscribe leaves nothing behind; a failed unsubscribe keeps
           the slot so the operator can retry. */
        if (subscribing) {
            memset(&s_subscriptions[index], 0, sizeof(s_subscriptions[index]));
        } else {
            s_subscriptions[index].subscribing = false;
        }
    } else if (subscribing) {
        s_subscriptions[index].active = true;
        s_subscriptions[index].subscribing = false;
    } else {
        memset(&s_subscriptions[index], 0, sizeof(s_subscriptions[index]));
    }
    diag_unlock();
    if (error->status != 0) {
        diag_set_error("CCCD %s failed handle=0x%04X status=%d",
                       subscribing ? "subscribe" : "unsubscribe", error->att_handle,
                       error->status);
        return 0;
    }
    DIAG_INFO("GATT", "%s confirmed characteristic=%s value_handle=0x%04X cccd=0x%04X",
              subscribing ? "SUBSCRIBE" : "UNSUBSCRIBE", uuid, value_handle,
              attr != NULL ? attr->handle : 0);
    return 0;
}

static void diag_log_notification(const struct ble_gap_event *event)
{
    uint8_t data[APP_BLE_DIAG_PAYLOAD_MAX_BYTES];
    uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
    uint16_t copied = length > sizeof(data) ? (uint16_t)sizeof(data) : length;
    if (copied > 0 && os_mbuf_copydata(event->notify_rx.om, 0, copied, data) != 0) {
        diag_set_error("NOTIFY mbuf copy failed handle=0x%04X",
                       event->notify_rx.attr_handle);
        return;
    }
    char uuid[BLE_UUID_STR_LEN];
    bool known = false;
    diag_lock();
    for (size_t i = 0; i < DIAG_MAX_SUBSCRIPTIONS; ++i) {
        if (s_subscriptions[i].in_use &&
            s_subscriptions[i].value_handle == event->notify_rx.attr_handle) {
            diag_uuid_string(&s_subscriptions[i].uuid.u, uuid);
            known = true;
            break;
        }
    }
    diag_unlock();
    char hex[DIAG_HEX_TEXT(APP_BLE_DIAG_PAYLOAD_MAX_BYTES)];
    char ascii[APP_BLE_DIAG_PAYLOAD_MAX_BYTES + 1];
    ble_diag_hex_encode(data, copied, hex, sizeof(hex));
    ble_diag_ascii_encode(data, copied, ascii, sizeof(ascii));
    DIAG_INFO("NOTIFY", "%s characteristic=%s handle=0x%04X length=%u%s",
              event->notify_rx.indication ? "INDICATION" : "NOTIFICATION",
              known ? uuid : "<not subscribed by diag>", event->notify_rx.attr_handle, length,
              copied < length ? " (truncated in log)" : "");
    DIAG_INFO("NOTIFY", "HEX: %s", hex);
    DIAG_INFO("NOTIFY", "ASCII: %s", ascii);
}

/* ------------------------------------------------------------------ */
/* GAP events                                                          */
/* ------------------------------------------------------------------ */

static int diag_mtu_exchanged(uint16_t conn_handle, const struct ble_gatt_error *error,
                              uint16_t mtu, void *arg)
{
    (void)arg;
    if (error->status != 0) {
        DIAG_WARN("CONNECT", "MTU exchange conn=%u status=%d", conn_handle, error->status);
        return 0;
    }
    diag_lock();
    if (s_conn_handle == conn_handle) s_mtu = mtu;
    diag_unlock();
    DIAG_INFO("CONNECT", "MTU conn=%u value=%u (max GATT payload %u bytes)", conn_handle, mtu,
              mtu > 3 ? mtu - 3 : 0);
    return 0;
}

static void diag_reset_link_state(void)
{
    diag_lock();
    s_link_state = DIAG_LINK_DISCONNECTED;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_mtu = 0;
    s_discovery_state = DIAG_DISCOVERY_NONE;
    s_service_count = 0;
    s_characteristic_count = 0;
    s_descriptor_count = 0;
    memset(s_subscriptions, 0, sizeof(s_subscriptions));
    diag_unlock();
}

static int diag_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields = {0};
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
            char address[DIAG_ADDRESS_TEXT_MAX];
            diag_format_address(&event->disc.addr, address, sizeof(address));
            DIAG_WARN("SCAN", "Advertisement parse failed address=%s", address);
            return 0;
        }
        diag_scan_report(&event->disc, &fields);
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        diag_scan_finished("duration elapsed");
        return 0;

    case BLE_GAP_EVENT_CONNECT: {
        char address[DIAG_ADDRESS_TEXT_MAX];
        diag_lock();
        diag_format_address(&s_target, address, sizeof(address));
        if (s_master_op == DIAG_MASTER_CONNECTING) s_master_op = DIAG_MASTER_IDLE;
        diag_unlock();
        if (event->connect.status != 0) {
            diag_reset_link_state();
            diag_set_error("CONNECT failed address=%s status=0x%X", address,
                           event->connect.status);
            return 0;
        }
        diag_lock();
        s_link_state = DIAG_LINK_CONNECTED;
        s_conn_handle = event->connect.conn_handle;
        s_discovery_state = DIAG_DISCOVERY_NONE;
        diag_unlock();
        DIAG_INFO("CONNECT", "Connected address=%s handle=%u", address,
                  event->connect.conn_handle);
        struct ble_gap_conn_desc description;
        if (ble_gap_conn_find(event->connect.conn_handle, &description) == 0) {
            unsigned interval_micro_ms = (unsigned)description.conn_itvl * 125u;
            DIAG_INFO("CONNECT",
                      "Parameters interval=%u (%u.%02u ms) latency=%u supervision=%u (%u ms)",
                      description.conn_itvl, interval_micro_ms / 100u,
                      interval_micro_ms % 100u, description.conn_latency,
                      description.supervision_timeout,
                      (unsigned)description.supervision_timeout * 10u);
        }
        int rc = ble_gattc_exchange_mtu(event->connect.conn_handle, diag_mtu_exchanged, NULL);
        if (rc != 0) DIAG_WARN("CONNECT", "MTU exchange start failed rc=%d", rc);
        DIAG_INFO("CONNECT", "Run 'diag gatt' to discover services when ready");
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT: {
        char address[DIAG_ADDRESS_TEXT_MAX];
        diag_lock();
        diag_format_address(&s_target, address, sizeof(address));
        s_last_disconnect_reason = event->disconnect.reason;
        diag_unlock();
        diag_reset_link_state();
        DIAG_INFO("DISCONNECT", "address=%s handle=%u reason=0x%X", address,
                  event->disconnect.conn.conn_handle, event->disconnect.reason);
        return 0;
    }

    case BLE_GAP_EVENT_NOTIFY_RX:
        diag_log_notification(event);
        return 0;

    case BLE_GAP_EVENT_MTU:
        diag_lock();
        if (s_conn_handle == event->mtu.conn_handle) s_mtu = event->mtu.value;
        diag_unlock();
        DIAG_INFO("CONNECT", "MTU event handle=%u value=%u", event->mtu.conn_handle,
                  event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        DIAG_INFO("CONNECT", "Connection update handle=%u status=%d",
                  event->conn_update.conn_handle, event->conn_update.status);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        DIAG_WARN("CONNECT", "Repeat pairing ignored; diagnostics does not bond");
        return BLE_GAP_REPEAT_PAIRING_IGNORE;

    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Master role arbitration                                             */
/* ------------------------------------------------------------------ */

void ble_diagnostics_release_master(const char *reason)
{
    /* Runs on the connection_mgr task every tick while a headlight is down, so
       the idle case must not touch the diagnostic mutex. */
    if (!s_initialized || s_master_op == DIAG_MASTER_IDLE) return;
    diag_master_op_t op;
    diag_lock();
    op = s_master_op;
    if (op != DIAG_MASTER_IDLE) s_master_op = DIAG_MASTER_IDLE;
    if (op == DIAG_MASTER_CONNECTING) s_link_state = DIAG_LINK_DISCONNECTED;
    diag_unlock();
    if (op == DIAG_MASTER_IDLE) return;
    /* NimBLE calls stay outside the diagnostic lock. */
    if (op == DIAG_MASTER_SCANNING) {
        (void)ble_gap_disc_cancel();
        DIAG_WARN("SCAN", "Scan aborted: %s", reason != NULL ? reason : "SP624E priority");
    } else {
        (void)ble_gap_conn_cancel();
        DIAG_WARN("CONNECT",
                  "Connection attempt aborted: %s", reason != NULL ? reason : "SP624E priority");
    }
}

bool ble_diagnostics_is_enabled(void) { return s_enabled; }

/* ------------------------------------------------------------------ */
/* Command implementations                                             */
/* ------------------------------------------------------------------ */

static bool diag_require_enabled(void)
{
    if (s_enabled) return true;
    DIAG_WARN("ERROR", "Diagnostic mode is disabled; run 'diag enable' first");
    return false;
}

static void diag_warn_if_headlights_degraded(void)
{
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        ble_connection_manager_status_t status;
        ble_connection_manager_get_status(side, &status);
        if (status.connected) continue;
        DIAG_WARN("STATUS",
                  "%s is %s: SP624E recovery has priority and will abort diagnostic radio use",
                  sp624e_side_name(side), ble_connection_state_name(status.state));
    }
}

static void diag_command_scan_start(const char *seconds_text)
{
    if (!diag_require_enabled()) return;
    if (!diag_ble_ready()) {
        diag_set_error("BLE host is not synced yet");
        return;
    }
    uint32_t duration_ms = APP_BLE_DIAG_SCAN_DURATION_MS;
    if (seconds_text != NULL) {
        long seconds = strtol(seconds_text, NULL, 10);
        if (seconds <= 0 || seconds * 1000 > APP_BLE_DIAG_MAX_SCAN_MS) {
            diag_set_error("scan duration must be 1..%d seconds",
                           APP_BLE_DIAG_MAX_SCAN_MS / 1000);
            return;
        }
        duration_ms = (uint32_t)seconds * 1000;
    }
    diag_lock();
    bool busy = s_master_op != DIAG_MASTER_IDLE;
    if (!busy) {
        s_master_op = DIAG_MASTER_SCANNING;
        s_scan_requested_ms = duration_ms;
        s_scan_started_ms = diag_now_ms();
        s_scan_packets = 0;
        s_scan_sessions++;
    }
    diag_unlock();
    if (busy) {
        diag_set_error("diagnostic radio busy; run 'diag scan stop' or wait");
        return;
    }
    diag_warn_if_headlights_degraded();

    uint8_t own_addr_type = 0;
    int rc = diag_own_addr_type(&own_addr_type);
    if (rc == 0) {
        struct ble_gap_disc_params params = {0};
        params.passive = 0;
        params.filter_duplicates = 0;
        params.itvl = APP_BLE_DIAG_SCAN_INTERVAL_UNITS;
        params.window = APP_BLE_DIAG_SCAN_WINDOW_UNITS;
        rc = ble_gap_disc(own_addr_type, (int32_t)duration_ms, &params, diag_gap_event, NULL);
    }
    if (rc != 0) {
        diag_lock();
        s_master_op = DIAG_MASTER_IDLE;
        diag_unlock();
        diag_set_error("scan start failed rc=%d", rc);
        return;
    }
    DIAG_INFO("SCAN", "Active scan started duration=%" PRIu32 "ms interval=%u window=%u",
              duration_ms, APP_BLE_DIAG_SCAN_INTERVAL_UNITS, APP_BLE_DIAG_SCAN_WINDOW_UNITS);
}

static void diag_command_scan_stop(void)
{
    diag_lock();
    bool scanning = s_master_op == DIAG_MASTER_SCANNING;
    if (scanning) s_master_op = DIAG_MASTER_IDLE;
    diag_unlock();
    if (!scanning) {
        DIAG_WARN("SCAN", "No diagnostic scan is running");
        return;
    }
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        diag_set_error("scan cancel failed rc=%d", rc);
        return;
    }
    diag_scan_finished("stopped by operator");
}

static void diag_command_scan_clear(void)
{
    diag_lock();
    if (diag_tables_ready()) {
        memset(s_devices, 0, APP_BLE_DIAG_MAX_DEVICES * sizeof(*s_devices));
    }
    s_scan_packets = 0;
    s_scan_sessions = 0;
    s_scan_table_full = false;
    diag_unlock();
    DIAG_INFO("SCAN", "Device table cleared");
}

static void diag_command_scan_list(const char *address_text)
{
    ble_addr_t filter = {0};
    bool filtered = false;
    if (address_text != NULL) {
        if (ble_diag_address_parse(address_text, filter.val) != 0) {
            diag_set_error("invalid address: %s", address_text);
            return;
        }
        filtered = true;
    }
    size_t total = 0;
    diag_lock();
    total = diag_device_count();
    uint32_t sessions = s_scan_sessions;
    uint32_t packets = s_scan_packets;
    diag_unlock();
    DIAG_INFO("SCAN", "========================================");
    DIAG_INFO("SCAN", "DEVICE TABLE unique=%u sessions=%" PRIu32 " packets_last_scan=%" PRIu32,
              (unsigned)total, sessions, packets);
    size_t shown = 0;
    for (size_t i = 0; i < APP_BLE_DIAG_MAX_DEVICES; ++i) {
        diag_device_t device;
        diag_lock();
        bool ready = diag_tables_ready();
        if (ready) device = s_devices[i];
        diag_unlock();
        if (!ready) break;
        if (!device.in_use) continue;
        if (filtered && memcmp(device.address.val, filter.val, 6) != 0) continue;
        shown++;
        if (filtered) {
            diag_log_device_detail(&device, "DEVICE DETAIL");
            continue;
        }
        char address[DIAG_ADDRESS_TEXT_MAX];
        diag_format_address(&device.address, address, sizeof(address));
        DIAG_INFO("SCAN",
                  "#%02u %s %-7s rssi=%4d best=%4d adv=%3u rsp=%3u name=%s",
                  (unsigned)shown, address, diag_address_type_name(device.address.type),
                  device.rssi_last, device.rssi_best, device.adv_packets, device.rsp_packets,
                  device.name[0] != '\0' ? device.name : "<none>");
    }
    if (shown == 0) {
        DIAG_INFO("SCAN", "<no devices>%s", filtered ? " matching that address" : "");
        DIAG_INFO("SCAN", "Tables are %s",
                  total > 0 ? "populated" : "empty (run 'diag enable' then 'diag scan start')");
    } else if (!filtered) {
        DIAG_INFO("SCAN", "Use 'diag scan list <address>' for the full payload of one device");
    }
    DIAG_INFO("SCAN", "========================================");
}

static void diag_command_target(const char *address_text)
{
    if (address_text != NULL && runtime_console_command_is(address_text, "clear")) {
        diag_lock();
        s_target_valid = false;
        memset(&s_target, 0, sizeof(s_target));
        diag_unlock();
        DIAG_INFO("CONNECT", "Target cleared");
        return;
    }
    if (address_text == NULL) {
        diag_set_error("usage: diag target <address>|clear");
        return;
    }
    ble_addr_t address = {0};
    if (ble_diag_address_parse(address_text, address.val) != 0) {
        diag_set_error("invalid address: %s", address_text);
        return;
    }
    diag_lock();
    if (s_link_state != DIAG_LINK_DISCONNECTED) {
        diag_unlock();
        diag_set_error("disconnect the current diagnostic link before changing the target");
        return;
    }
    /* Reuse the observed address type when the device is already in the table. */
    address.type = BLE_ADDR_PUBLIC;
    bool observed = false;
    for (size_t i = 0; diag_tables_ready() && i < APP_BLE_DIAG_MAX_DEVICES; ++i) {
        if (s_devices[i].in_use && memcmp(s_devices[i].address.val, address.val, 6) == 0) {
            address.type = s_devices[i].address.type;
            observed = true;
            break;
        }
    }
    s_target = address;
    s_target_valid = true;
    diag_unlock();
    char text[DIAG_ADDRESS_TEXT_MAX];
    diag_format_address(&address, text, sizeof(text));
    DIAG_INFO("CONNECT", "Target set address=%s type=%s", text,
              diag_address_type_name(address.type));
    if (!observed) {
        DIAG_WARN("CONNECT",
                  "Address not in the scan table; assuming a public address type. Run "
                  "'diag scan start' first if the connection fails.");
    }
}

static void diag_command_connect(void)
{
    if (!diag_require_enabled()) return;
    if (!diag_ble_ready()) {
        diag_set_error("BLE host is not synced yet");
        return;
    }
    ble_addr_t target;
    diag_lock();
    bool valid = s_target_valid;
    bool busy = s_master_op != DIAG_MASTER_IDLE || s_link_state != DIAG_LINK_DISCONNECTED;
    target = s_target;
    if (valid && !busy) {
        s_master_op = DIAG_MASTER_CONNECTING;
        s_link_state = DIAG_LINK_CONNECTING;
    }
    diag_unlock();
    if (!valid) {
        diag_set_error("no target selected; run 'diag target <address>'");
        return;
    }
    if (busy) {
        diag_set_error("diagnostic radio busy or already connected");
        return;
    }
    diag_warn_if_headlights_degraded();

    char address[DIAG_ADDRESS_TEXT_MAX];
    diag_format_address(&target, address, sizeof(address));
    uint8_t own_addr_type = 0;
    int rc = diag_own_addr_type(&own_addr_type);
    if (rc == 0) {
        DIAG_INFO("CONNECT", "Connecting address=%s type=%s timeout=%dms", address,
                  diag_address_type_name(target.type), APP_BLE_DIAG_CONNECT_TIMEOUT_MS);
        rc = ble_gap_connect(own_addr_type, &target, APP_BLE_DIAG_CONNECT_TIMEOUT_MS, NULL,
                             diag_gap_event, NULL);
    }
    if (rc != 0) {
        diag_lock();
        s_master_op = DIAG_MASTER_IDLE;
        s_link_state = DIAG_LINK_DISCONNECTED;
        diag_unlock();
        diag_set_error("connect start failed address=%s rc=%d", address, rc);
    }
}

static void diag_command_disconnect(void)
{
    uint16_t handle;
    diag_lock();
    diag_link_state_t state = s_link_state;
    handle = s_conn_handle;
    diag_unlock();
    if (state == DIAG_LINK_CONNECTING) {
        ble_diagnostics_release_master("operator requested disconnect");
        return;
    }
    if (state != DIAG_LINK_CONNECTED) {
        DIAG_WARN("DISCONNECT", "No diagnostic connection is open");
        return;
    }
    int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) diag_set_error("terminate failed handle=%u rc=%d", handle, rc);
}

static void diag_command_gatt(void)
{
    if (!diag_require_enabled()) return;
    diag_lock();
    bool connected = s_link_state == DIAG_LINK_CONNECTED;
    bool running = s_discovery_state == DIAG_DISCOVERY_RUNNING;
    diag_unlock();
    if (!connected) {
        diag_set_error("not connected; run 'diag connect' first");
        return;
    }
    if (running) {
        DIAG_WARN("GATT", "Discovery already running");
        return;
    }
    diag_start_service_discovery();
}

/*
 * Resolves a service/characteristic pair from the discovered profile. Returns
 * true and fills the record when exactly one or more matches exist; the first
 * match wins and duplicates are reported.
 */
static bool diag_resolve_characteristic(const char *service_text, const char *chr_text,
                                        diag_characteristic_t *out)
{
    ble_uuid_any_t service_uuid;
    ble_uuid_any_t chr_uuid;
    if (ble_uuid_from_str(&service_uuid, service_text) != 0) {
        diag_set_error("invalid service UUID: %s", service_text);
        return false;
    }
    if (ble_uuid_from_str(&chr_uuid, chr_text) != 0) {
        diag_set_error("invalid characteristic UUID: %s", chr_text);
        return false;
    }
    size_t matches = 0;
    diag_lock();
    if (s_discovery_state != DIAG_DISCOVERY_DONE || !diag_tables_ready()) {
        diag_unlock();
        diag_set_error("GATT profile unknown; run 'diag gatt' first");
        return false;
    }
    for (uint8_t i = 0; i < s_characteristic_count; ++i) {
        const diag_characteristic_t *record = &s_characteristics[i];
        const struct ble_gatt_svc *service = &s_services[record->service_index].service;
        if (ble_uuid_cmp(&service->uuid.u, &service_uuid.u) != 0) continue;
        if (ble_uuid_cmp(&record->characteristic.uuid.u, &chr_uuid.u) != 0) continue;
        if (matches == 0) *out = *record;
        matches++;
    }
    diag_unlock();
    if (matches == 0) {
        diag_set_error("characteristic %s not found under service %s", chr_text, service_text);
        return false;
    }
    if (matches > 1) {
        DIAG_WARN("GATT", "%u characteristics match %s/%s; using value handle 0x%04X",
                  (unsigned)matches, service_text, chr_text,
                  out->characteristic.val_handle);
    }
    return true;
}

static bool diag_require_connection(uint16_t *conn_handle)
{
    diag_lock();
    bool connected = s_link_state == DIAG_LINK_CONNECTED;
    *conn_handle = s_conn_handle;
    diag_unlock();
    if (!connected) diag_set_error("not connected; run 'diag connect' first");
    return connected;
}

static void diag_command_read(const char *service_text, const char *chr_text)
{
    if (!diag_require_enabled()) return;
    if (service_text == NULL || chr_text == NULL) {
        diag_set_error("usage: diag read <service_uuid> <characteristic_uuid>");
        return;
    }
    uint16_t conn_handle;
    if (!diag_require_connection(&conn_handle)) return;
    diag_characteristic_t record;
    if (!diag_resolve_characteristic(service_text, chr_text, &record)) return;
    if ((record.characteristic.properties & BLE_DIAG_PROP_READ) == 0) {
        diag_set_error("characteristic %s is not readable (properties=0x%02X)", chr_text,
                       record.characteristic.properties);
        return;
    }
    DIAG_INFO("READ", "Requesting handle=0x%04X", record.characteristic.val_handle);
    int rc = ble_gattc_read(conn_handle, record.characteristic.val_handle, diag_read_result,
                            NULL);
    if (rc != 0) diag_set_error("read start failed rc=%d", rc);
}

/* Caller holds no lock. Returns the CCCD handle or 0 when absent. */
static uint16_t diag_find_cccd(uint16_t value_handle)
{
    uint16_t handle = 0;
    ble_uuid16_t cccd = BLE_UUID16_INIT(BLE_GATT_DSC_CLT_CFG_UUID16);
    diag_lock();
    for (uint8_t i = 0; i < s_descriptor_count; ++i) {
        if (s_descriptors[i].characteristic_value_handle != value_handle) continue;
        if (ble_uuid_cmp(&s_descriptors[i].descriptor.uuid.u, &cccd.u) != 0) continue;
        handle = s_descriptors[i].descriptor.handle;
        break;
    }
    diag_unlock();
    return handle;
}

static void diag_command_subscribe(const char *service_text, const char *chr_text,
                                   bool subscribe)
{
    if (!diag_require_enabled()) return;
    if (service_text == NULL || chr_text == NULL) {
        diag_set_error("usage: diag %s <service_uuid> <characteristic_uuid>",
                       subscribe ? "subscribe" : "unsubscribe");
        return;
    }
    uint16_t conn_handle;
    if (!diag_require_connection(&conn_handle)) return;
    diag_characteristic_t record;
    if (!diag_resolve_characteristic(service_text, chr_text, &record)) return;
    uint8_t properties = record.characteristic.properties;
    bool indicate = (properties & BLE_DIAG_PROP_NOTIFY) == 0 &&
                    (properties & BLE_DIAG_PROP_INDICATE) != 0;
    if ((properties & (BLE_DIAG_PROP_NOTIFY | BLE_DIAG_PROP_INDICATE)) == 0) {
        diag_set_error("characteristic %s supports neither NOTIFY nor INDICATE "
                       "(properties=0x%02X)", chr_text, properties);
        return;
    }
    uint16_t value_handle = record.characteristic.val_handle;
    uint16_t cccd_handle = diag_find_cccd(value_handle);
    if (cccd_handle == 0) {
        diag_set_error("no CCCD descriptor found for value handle 0x%04X", value_handle);
        return;
    }

    size_t index = DIAG_MAX_SUBSCRIPTIONS;
    diag_lock();
    for (size_t i = 0; i < DIAG_MAX_SUBSCRIPTIONS; ++i) {
        if (s_subscriptions[i].in_use && s_subscriptions[i].value_handle == value_handle) {
            index = i;
            break;
        }
    }
    bool already = index < DIAG_MAX_SUBSCRIPTIONS;
    if (subscribe && !already) {
        for (size_t i = 0; i < DIAG_MAX_SUBSCRIPTIONS; ++i) {
            if (!s_subscriptions[i].in_use) {
                index = i;
                break;
            }
        }
        if (index < DIAG_MAX_SUBSCRIPTIONS) {
            diag_subscription_t *slot = &s_subscriptions[index];
            memset(slot, 0, sizeof(*slot));
            slot->in_use = true;
            slot->subscribing = true;
            slot->value_handle = value_handle;
            slot->cccd_handle = cccd_handle;
            slot->indicate = indicate;
            ble_uuid_copy(&slot->uuid, &record.characteristic.uuid.u);
        }
    } else if (!subscribe && already) {
        s_subscriptions[index].subscribing = false;
    }
    diag_unlock();

    if (subscribe && already) {
        DIAG_WARN("GATT", "Already subscribed to value handle 0x%04X", value_handle);
        return;
    }
    if (!subscribe && !already) {
        DIAG_WARN("GATT", "Value handle 0x%04X was not subscribed", value_handle);
        return;
    }
    if (index >= DIAG_MAX_SUBSCRIPTIONS) {
        diag_set_error("subscription table full (%d entries)", DIAG_MAX_SUBSCRIPTIONS);
        return;
    }
    uint8_t payload[2] = {0, 0};
    if (subscribe) payload[0] = indicate ? 0x02 : 0x01;
    DIAG_INFO("GATT", "%s cccd=0x%04X value=%02X %02X (%s)",
              subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE", cccd_handle, payload[0], payload[1],
              indicate ? "indication" : "notification");
    int rc = ble_gattc_write_flat(conn_handle, cccd_handle, payload, sizeof(payload),
                                  diag_cccd_result, (void *)(uintptr_t)index);
    if (rc != 0) {
        diag_lock();
        if (subscribe) memset(&s_subscriptions[index], 0, sizeof(s_subscriptions[index]));
        diag_unlock();
        diag_set_error("CCCD write start failed rc=%d", rc);
    }
}

static void diag_command_write(const char *service_text, const char *chr_text,
                               const char *hex_text, bool with_response)
{
    if (!diag_require_enabled()) return;
    if (service_text == NULL || chr_text == NULL || hex_text == NULL) {
        diag_set_error("usage: diag %s <service_uuid> <characteristic_uuid> <hex>",
                       with_response ? "write" : "write_nr");
        return;
    }
    uint16_t conn_handle;
    if (!diag_require_connection(&conn_handle)) return;
    diag_characteristic_t record;
    if (!diag_resolve_characteristic(service_text, chr_text, &record)) return;
    uint8_t required = with_response ? BLE_DIAG_PROP_WRITE : BLE_DIAG_PROP_WRITE_NO_RSP;
    if ((record.characteristic.properties & required) == 0) {
        diag_set_error("characteristic %s does not support %s (properties=0x%02X)", chr_text,
                       with_response ? "WRITE" : "WRITE_NO_RESPONSE",
                       record.characteristic.properties);
        return;
    }
    uint8_t payload[APP_BLE_DIAG_PAYLOAD_MAX_BYTES];
    size_t length = 0;
    ble_diag_hex_result_t parsed =
        ble_diag_hex_decode(hex_text, payload, sizeof(payload), &length);
    if (parsed != BLE_DIAG_HEX_OK) {
        diag_set_error("invalid payload: %s", ble_diag_hex_result_name(parsed));
        return;
    }
    uint16_t mtu;
    diag_lock();
    mtu = s_mtu;
    diag_unlock();
    if (mtu > 3 && length > (size_t)(mtu - 3)) {
        diag_set_error("payload %u bytes exceeds the negotiated ATT payload of %u bytes",
                       (unsigned)length, (unsigned)(mtu - 3));
        return;
    }
    char hex[DIAG_HEX_TEXT(APP_BLE_DIAG_PAYLOAD_MAX_BYTES)];
    ble_diag_hex_encode(payload, length, hex, sizeof(hex));
    DIAG_INFO("WRITE", "%s handle=0x%04X length=%u bytes=%s",
              with_response ? "WRITE" : "WRITE_NO_RESPONSE",
              record.characteristic.val_handle, (unsigned)length, hex);
    int rc;
    if (with_response) {
        rc = ble_gattc_write_flat(conn_handle, record.characteristic.val_handle, payload,
                                  (uint16_t)length, diag_write_result, NULL);
    } else {
        rc = ble_gattc_write_no_rsp_flat(conn_handle, record.characteristic.val_handle,
                                         payload, (uint16_t)length);
        if (rc == 0) DIAG_INFO("WRITE", "Queued without response (no acknowledgement)");
    }
    if (rc != 0) diag_set_error("write start failed rc=%d", rc);
}

static void diag_command_status(void)
{
    diag_lock();
    bool enabled = s_enabled;
    diag_master_op_t master = s_master_op;
    diag_link_state_t link = s_link_state;
    uint16_t conn_handle = s_conn_handle;
    uint16_t mtu = s_mtu;
    bool target_valid = s_target_valid;
    ble_addr_t target = s_target;
    diag_discovery_state_t discovery = s_discovery_state;
    uint8_t services = s_service_count;
    uint8_t characteristics = s_characteristic_count;
    uint8_t descriptors = s_descriptor_count;
    uint32_t scan_ms = s_scan_requested_ms;
    size_t devices = diag_device_count();
    bool tables = diag_tables_ready();
    int disconnect_reason = s_last_disconnect_reason;
    char last_error[DIAG_ERROR_TEXT_MAX];
    snprintf(last_error, sizeof(last_error), "%s", s_last_error);
    size_t subscriptions = 0;
    for (size_t i = 0; i < DIAG_MAX_SUBSCRIPTIONS; ++i) {
        if (s_subscriptions[i].in_use) subscriptions++;
    }
    diag_unlock();

    char target_text[DIAG_ADDRESS_TEXT_MAX];
    diag_format_address(&target, target_text, sizeof(target_text));

    unsigned active_connections = link == DIAG_LINK_CONNECTED ? 1 : 0;
    DIAG_INFO("STATUS", "========================================");
    DIAG_INFO("STATUS", "BLE DIAGNOSTIC");
    DIAG_INFO("STATUS", "Diagnostic mode: %s", enabled ? "ENABLED" : "DISABLED");
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        ble_connection_manager_status_t status;
        ble_connection_manager_get_status(side, &status);
        if (status.connected) active_connections++;
        DIAG_INFO("STATUS", "%s: %s connected=%d handle=%u rssi=%d",
                  sp624e_side_name(side), ble_connection_state_name(status.state),
                  status.connected, status.conn_handle, status.rssi);
    }
    DIAG_INFO("STATUS", "Target: %s%s", target_valid ? target_text : "<none>",
              target_valid ? "" : " (use 'diag target <address>')");
    DIAG_INFO("STATUS", "Target connection: %s handle=%u",
              link == DIAG_LINK_CONNECTED ? "CONNECTED"
                                          : (link == DIAG_LINK_CONNECTING ? "CONNECTING"
                                                                          : "DISCONNECTED"),
              conn_handle);
    DIAG_INFO("STATUS", "MTU: %u", mtu);
    DIAG_INFO("STATUS", "Last diagnostic disconnect reason: 0x%X", disconnect_reason);
    DIAG_INFO("STATUS", "GATT profile: %s services=%u characteristics=%u descriptors=%u",
              discovery == DIAG_DISCOVERY_DONE
                  ? "DISCOVERED"
                  : (discovery == DIAG_DISCOVERY_RUNNING
                         ? "RUNNING"
                         : (discovery == DIAG_DISCOVERY_FAILED ? "FAILED" : "UNKNOWN")),
              services, characteristics, descriptors);
    DIAG_INFO("STATUS", "Subscriptions active: %u/%d", (unsigned)subscriptions,
              DIAG_MAX_SUBSCRIPTIONS);
    DIAG_INFO("STATUS", "Scan: %s last_duration=%" PRIu32 "ms devices=%u/%d",
              master == DIAG_MASTER_SCANNING ? "ACTIVE" : "INACTIVE", scan_ms,
              (unsigned)devices, APP_BLE_DIAG_MAX_DEVICES);
    DIAG_INFO("STATUS", "Diagnostic radio: %s",
              master == DIAG_MASTER_SCANNING
                  ? "SCANNING"
                  : (master == DIAG_MASTER_CONNECTING ? "CONNECTING" : "IDLE"));
    DIAG_INFO("STATUS", "Connections in use: %u of %d configured", active_connections,
              CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
    DIAG_INFO("STATUS", "Tables: %s", tables ? "ALLOCATED" : "RELEASED (diagnostic disabled)");
    DIAG_INFO("STATUS", "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());
    DIAG_INFO("STATUS", "Largest free block: %u bytes",
              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    DIAG_INFO("STATUS", "Minimum free heap: %" PRIu32 " bytes",
              esp_get_minimum_free_heap_size());
    DIAG_INFO("STATUS", "Last diagnostic error: %s", last_error);
    DIAG_INFO("STATUS", "========================================");
}

static void diag_command_help(void)
{
    DIAG_INFO("HELP", "diag help                                  this list");
    DIAG_INFO("HELP", "diag status                                diagnostic + SP624E state");
    DIAG_INFO("HELP", "diag enable | disable                      arm/disarm the radio commands");
    DIAG_INFO("HELP", "diag scan start [seconds]                  active scan (default %d s, max %d s)",
              APP_BLE_DIAG_SCAN_DURATION_MS / 1000, APP_BLE_DIAG_MAX_SCAN_MS / 1000);
    DIAG_INFO("HELP", "diag scan stop                             stop the scan now");
    DIAG_INFO("HELP", "diag scan list [address]                   table, or one device in full");
    DIAG_INFO("HELP", "diag scan clear                            empty the device table");
    DIAG_INFO("HELP", "diag target <address> | clear              select the device to inspect");
    DIAG_INFO("HELP", "diag connect | disconnect                  manual link to the target");
    DIAG_INFO("HELP", "diag gatt                                  full service discovery");
    DIAG_INFO("HELP", "diag read <service> <characteristic>       manual read");
    DIAG_INFO("HELP", "diag subscribe <service> <characteristic>  enable notify/indicate");
    DIAG_INFO("HELP", "diag unsubscribe <service> <characteristic>");
    DIAG_INFO("HELP", "diag write <service> <characteristic> <hex>     write with response");
    DIAG_INFO("HELP", "diag write_nr <service> <characteristic> <hex>  write without response");
    DIAG_INFO("HELP", "Addresses use AA:BB:CC:DD:EE:FF; UUIDs accept FFE0 or the 128-bit form.");
    DIAG_INFO("HELP", "SP624E recovery always wins: it aborts diagnostic scans and connects.");
}

/*
 * Diagnostic tables are acquired on enable and released on disable so the
 * disabled firmware costs nothing at runtime. Callers must not hold the lock.
 */
static bool diag_resources_acquire(void)
{
    size_t devices_bytes = APP_BLE_DIAG_MAX_DEVICES * sizeof(diag_device_t);
    size_t services_bytes = APP_BLE_MAX_SERVICES * sizeof(diag_service_t);
    size_t chr_bytes = APP_BLE_MAX_CHARACTERISTICS * sizeof(diag_characteristic_t);
    size_t dsc_bytes = APP_BLE_MAX_DESCRIPTORS * sizeof(diag_descriptor_t);
    uint32_t before = esp_get_free_heap_size();

    diag_device_t *devices = calloc(APP_BLE_DIAG_MAX_DEVICES, sizeof(diag_device_t));
    diag_service_t *services = calloc(APP_BLE_MAX_SERVICES, sizeof(diag_service_t));
    diag_characteristic_t *characteristics =
        calloc(APP_BLE_MAX_CHARACTERISTICS, sizeof(diag_characteristic_t));
    diag_descriptor_t *descriptors =
        calloc(APP_BLE_MAX_DESCRIPTORS, sizeof(diag_descriptor_t));
    if (devices == NULL || services == NULL || characteristics == NULL ||
        descriptors == NULL) {
        free(devices);
        free(services);
        free(characteristics);
        free(descriptors);
        diag_set_error("cannot allocate %u bytes of diagnostic tables; free heap %" PRIu32,
                       (unsigned)(devices_bytes + services_bytes + chr_bytes + dsc_bytes),
                       before);
        return false;
    }
    diag_lock();
    s_devices = devices;
    s_services = services;
    s_characteristics = characteristics;
    s_descriptors = descriptors;
    s_service_count = 0;
    s_characteristic_count = 0;
    s_descriptor_count = 0;
    diag_unlock();
    DIAG_INFO("STATUS",
              "Tables allocated devices=%u services=%u characteristics=%u descriptors=%u "
              "total=%u bytes heap_before=%" PRIu32 " heap_after=%" PRIu32,
              (unsigned)devices_bytes, (unsigned)services_bytes, (unsigned)chr_bytes,
              (unsigned)dsc_bytes,
              (unsigned)(devices_bytes + services_bytes + chr_bytes + dsc_bytes), before,
              esp_get_free_heap_size());
    return true;
}

static void diag_resources_release(void)
{
    diag_device_t *devices;
    diag_service_t *services;
    diag_characteristic_t *characteristics;
    diag_descriptor_t *descriptors;
    diag_lock();
    devices = s_devices;
    services = s_services;
    characteristics = s_characteristics;
    descriptors = s_descriptors;
    /* NULL first: every table access is guarded by diag_tables_ready() under
       this same lock, so no callback can be mid-read once we unlock. */
    s_devices = NULL;
    s_services = NULL;
    s_characteristics = NULL;
    s_descriptors = NULL;
    s_service_count = 0;
    s_characteristic_count = 0;
    s_descriptor_count = 0;
    s_discovery_state = DIAG_DISCOVERY_NONE;
    s_scan_packets = 0;
    s_scan_sessions = 0;
    s_scan_table_full = false;
    diag_unlock();
    free(devices);
    free(services);
    free(characteristics);
    free(descriptors);
    DIAG_INFO("STATUS", "Tables released; free heap %" PRIu32, esp_get_free_heap_size());
}

static void diag_command_enable(bool enable)
{
    if (s_enabled == enable) {
        DIAG_INFO("STATUS", "Diagnostic mode already %s", enable ? "ENABLED" : "DISABLED");
        return;
    }
    if (enable) {
        if (!diag_resources_acquire()) return;
        s_enabled = true;
        DIAG_INFO("STATUS", "Diagnostic mode ENABLED");
        return;
    }
    s_enabled = false;
    ble_diagnostics_release_master("diagnostic mode disabled");
    diag_lock();
    bool connected = s_link_state == DIAG_LINK_CONNECTED;
    diag_unlock();
    if (connected) diag_command_disconnect();
    diag_resources_release();
    DIAG_INFO("STATUS", "Diagnostic mode DISABLED (scan table discarded)");
}

/* ------------------------------------------------------------------ */
/* Console entry point                                                 */
/* ------------------------------------------------------------------ */

static bool diag_console_handler(char *line, void *context)
{
    (void)context;
    char *save = NULL;
    char *command = strtok_r(line, " \t", &save);
    if (!runtime_console_command_is(command, "diag")) return false;

    char *action = strtok_r(NULL, " \t", &save);
    if (action == NULL || runtime_console_command_is(action, "help")) {
        diag_command_help();
    } else if (runtime_console_command_is(action, "status")) {
        diag_command_status();
    } else if (runtime_console_command_is(action, "enable")) {
        diag_command_enable(true);
    } else if (runtime_console_command_is(action, "disable")) {
        diag_command_enable(false);
    } else if (runtime_console_command_is(action, "scan")) {
        char *operation = strtok_r(NULL, " \t", &save);
        char *argument = strtok_r(NULL, " \t", &save);
        if (runtime_console_command_is(operation, "start")) {
            diag_command_scan_start(argument);
        } else if (runtime_console_command_is(operation, "stop")) {
            diag_command_scan_stop();
        } else if (runtime_console_command_is(operation, "list")) {
            diag_command_scan_list(argument);
        } else if (runtime_console_command_is(operation, "clear")) {
            diag_command_scan_clear();
        } else {
            diag_set_error("usage: diag scan start|stop|list|clear");
        }
    } else if (runtime_console_command_is(action, "target")) {
        diag_command_target(strtok_r(NULL, " \t", &save));
    } else if (runtime_console_command_is(action, "connect")) {
        diag_command_connect();
    } else if (runtime_console_command_is(action, "disconnect")) {
        diag_command_disconnect();
    } else if (runtime_console_command_is(action, "gatt")) {
        diag_command_gatt();
    } else if (runtime_console_command_is(action, "read")) {
        char *service = strtok_r(NULL, " \t", &save);
        diag_command_read(service, strtok_r(NULL, " \t", &save));
    } else if (runtime_console_command_is(action, "subscribe") ||
               runtime_console_command_is(action, "unsubscribe")) {
        bool subscribe = runtime_console_command_is(action, "subscribe");
        char *service = strtok_r(NULL, " \t", &save);
        char *characteristic = strtok_r(NULL, " \t", &save);
        diag_command_subscribe(service, characteristic, subscribe);
    } else if (runtime_console_command_is(action, "write") ||
               runtime_console_command_is(action, "write_nr")) {
        bool with_response = runtime_console_command_is(action, "write");
        char *service = strtok_r(NULL, " \t", &save);
        char *characteristic = strtok_r(NULL, " \t", &save);
        char *payload = strtok_r(NULL, " \t", &save);
        diag_command_write(service, characteristic, payload, with_response);
    } else {
        diag_set_error("unknown command 'diag %s'; run 'diag help'", action);
    }
    return true;
}

esp_err_t ble_diagnostics_init(void)
{
    if (s_initialized) return ESP_OK;
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    esp_err_t err = runtime_console_register(diag_console_handler, NULL, false);
    if (err != ESP_OK) return err;
    err = ble_connection_manager_add_master_guard(ble_diagnostics_release_master);
    if (err != ESP_OK) return err;
    s_initialized = true;
    ESP_LOGI(TAG, "BLE diagnostics registered (disabled by default; 'diag help' for commands)");
    return ESP_OK;
}
