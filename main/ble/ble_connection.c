#include "ble_connection.h"
#include "ble_connection_manager.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "nimble/ble.h"
#include "os/os_mbuf.h"
#include "sp624e/sp624e_controller.h"
#include "sp624e/sp624e_protocol.h"
#include "sp624e/sp624e_provisioning.h"

#if SP624E_ALLOW_WRITES != 1
#error "SP624E 0.4.0 requires controlled writes behind runtime safety gates"
#endif

static const char *TAG_CONNECT = "BLE_CONNECT";
static const char *TAG_GATT = "BLE_GATT";
static const char *TAG_SP624E = "SP624E";

typedef struct {
    struct ble_gatt_svc service;
} service_record_t;

typedef struct {
    struct ble_gatt_chr characteristic;
    uint8_t service_index;
} characteristic_record_t;

typedef struct {
    struct ble_gatt_dsc descriptor;
    uint16_t characteristic_value_handle;
} descriptor_record_t;

typedef struct {
    sp624e_side_t side;
    ble_device_entry_t *entry;
    service_record_t services[APP_BLE_MAX_SERVICES];
    characteristic_record_t characteristics[APP_BLE_MAX_CHARACTERISTICS];
    descriptor_record_t descriptors[APP_BLE_MAX_DESCRIPTORS];
    uint8_t service_count;
    uint8_t characteristic_count;
    uint8_t descriptor_count;
    uint8_t current_service;
    uint8_t current_characteristic;
    bool discovery_done;
    bool ffe0_found;
    bool ffe1_found;
    uint16_t ffe1_value_handle;
    uint8_t ffe1_properties;
    bool cccd_found;
    uint16_t cccd_handle;
    bool cache_valid;
    uint16_t cached_ffe1_value_handle;
    uint8_t cached_ffe1_properties;
    uint16_t cached_cccd_handle;
    bool using_cached_handles;
} connection_context_t;

static connection_context_t s_contexts[2];
static uint8_t s_own_addr_type;

static int gap_event(struct ble_gap_event *event, void *arg);
static int connect_context(connection_context_t *context, uint32_t timeout_ms);
static void start_service_discovery(connection_context_t *context);
static void start_next_characteristic_discovery(connection_context_t *context);
static void start_next_descriptor_discovery(connection_context_t *context);
static void finish_discovery(connection_context_t *context, int status);

static sp624e_transport_t controller_transport(connection_context_t *context)
{
    return (sp624e_transport_t) {
        .entry = context->entry,
        .conn_handle = context->entry->conn_handle,
        .ffe1_handle = context->ffe1_value_handle,
        .cccd_handle = context->cccd_handle,
        .signature_confirmed = context->entry->type == DEVICE_CONFIRMED_SP624E,
        .ffe0_found = context->ffe0_found,
        .ffe1_found = context->ffe1_found,
        .cccd_found = context->cccd_found,
        .cached_handles = context->using_cached_handles,
    };
}

static bool uuid_equals16(const ble_uuid_t *uuid, uint16_t value)
{
    ble_uuid16_t expected = BLE_UUID16_INIT(value);
    return ble_uuid_cmp(uuid, &expected.u) == 0;
}

static const char *reason_name(int reason)
{
    if (reason == BLE_HS_HCI_ERR(BLE_ERR_CONN_ESTABLISHMENT)) {
        return "CONNECTION_FAILED_TO_BE_ESTABLISHED";
    }
    if (reason == BLE_HS_HCI_ERR(BLE_ERR_CONN_SPVN_TMO)) {
        return "CONNECTION_SUPERVISION_TIMEOUT";
    }
    if (reason == BLE_HS_HCI_ERR(BLE_ERR_CONN_TERM_LOCAL)) {
        return "CONNECTION_TERMINATED_LOCALLY";
    }
    return "UNMAPPED";
}

static void uuid_string(const ble_uuid_t *uuid, char output[BLE_UUID_STR_LEN])
{
    ble_uuid_to_str(uuid, output);
}

static void properties_string(uint8_t properties, char *output, size_t size)
{
    size_t used = 0;
    const struct { uint8_t flag; const char *name; } values[] = {
        {BLE_GATT_CHR_PROP_READ, "READ"},
        {BLE_GATT_CHR_PROP_WRITE, "WRITE"},
        {BLE_GATT_CHR_PROP_WRITE_NO_RSP, "WRITE_NO_RESPONSE"},
        {BLE_GATT_CHR_PROP_NOTIFY, "NOTIFY"},
        {BLE_GATT_CHR_PROP_INDICATE, "INDICATE"},
    };
    output[0] = '\0';
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        if ((properties & values[i].flag) != 0 && used < size) {
            used += (size_t)snprintf(output + used, size - used, "%s%s",
                                    used > 0 ? "|" : "", values[i].name);
        }
    }
    if (used == 0) {
        snprintf(output, size, "NONE");
    }
}

static int service_discovered(uint16_t conn_handle, const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *service, void *arg)
{
    connection_context_t *context = arg;
    if (error->status == 0 && service != NULL) {
        char uuid[BLE_UUID_STR_LEN];
        uuid_string(&service->uuid.u, uuid);
        ESP_LOGI(TAG_GATT, "Service conn=%u UUID=%s start=0x%04X end=0x%04X",
                 conn_handle, uuid, service->start_handle, service->end_handle);
        if (context->service_count < APP_BLE_MAX_SERVICES) {
            context->services[context->service_count++].service = *service;
        } else {
            ESP_LOGW(TAG_GATT, "Service registry full; UUID=%s", uuid);
        }
        if (uuid_equals16(&service->uuid.u, 0xffe0)) {
            context->ffe0_found = true;
            ESP_LOGI(TAG_GATT, "FFE0 service found on conn=%u", conn_handle);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        context->current_service = 0;
        start_next_characteristic_discovery(context);
        return 0;
    }
    ESP_LOGE(TAG_GATT, "Service discovery failed conn=%u status=%u handle=0x%04X",
             conn_handle, error->status, error->att_handle);
    finish_discovery(context, error->status);
    return 0;
}

static int characteristic_discovered(uint16_t conn_handle,
                                     const struct ble_gatt_error *error,
                                     const struct ble_gatt_chr *characteristic,
                                     void *arg)
{
    connection_context_t *context = arg;
    if (error->status == 0 && characteristic != NULL) {
        char uuid[BLE_UUID_STR_LEN];
        char properties[80];
        uuid_string(&characteristic->uuid.u, uuid);
        properties_string(characteristic->properties, properties, sizeof(properties));
        ESP_LOGI(TAG_GATT,
                 "Characteristic conn=%u UUID=%s def=0x%04X value=0x%04X properties=0x%02X [%s]",
                 conn_handle, uuid, characteristic->def_handle, characteristic->val_handle,
                 characteristic->properties, properties);
        if (context->characteristic_count < APP_BLE_MAX_CHARACTERISTICS) {
            characteristic_record_t *record =
                &context->characteristics[context->characteristic_count++];
            record->characteristic = *characteristic;
            record->service_index = context->current_service;
        } else {
            ESP_LOGW(TAG_GATT, "Characteristic registry full; UUID=%s", uuid);
        }
        const struct ble_gatt_svc *service =
            &context->services[context->current_service].service;
        if (uuid_equals16(&service->uuid.u, 0xffe0) &&
            uuid_equals16(&characteristic->uuid.u, 0xffe1)) {
            context->ffe1_found = true;
            context->ffe1_value_handle = characteristic->val_handle;
            context->ffe1_properties = characteristic->properties;
            ESP_LOGI(TAG_GATT, "FFE1 characteristic found on conn=%u value_handle=0x%04X",
                     conn_handle, characteristic->val_handle);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        context->current_service++;
        start_next_characteristic_discovery(context);
        return 0;
    }
    ESP_LOGE(TAG_GATT, "Characteristic discovery failed conn=%u status=%u",
             conn_handle, error->status);
    finish_discovery(context, error->status);
    return 0;
}

static int descriptor_discovered(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 uint16_t characteristic_value_handle,
                                 const struct ble_gatt_dsc *descriptor,
                                 void *arg)
{
    connection_context_t *context = arg;
    if (error->status == 0 && descriptor != NULL) {
        char uuid[BLE_UUID_STR_LEN];
        uuid_string(&descriptor->uuid.u, uuid);
        ESP_LOGI(TAG_GATT, "Descriptor conn=%u chr_value=0x%04X UUID=%s handle=0x%04X",
                 conn_handle, characteristic_value_handle, uuid, descriptor->handle);
        if (context->descriptor_count < APP_BLE_MAX_DESCRIPTORS) {
            descriptor_record_t *record = &context->descriptors[context->descriptor_count++];
            record->descriptor = *descriptor;
            record->characteristic_value_handle = characteristic_value_handle;
        } else {
            ESP_LOGW(TAG_GATT, "Descriptor registry full; UUID=%s", uuid);
        }
        if (characteristic_value_handle == context->ffe1_value_handle &&
            uuid_equals16(&descriptor->uuid.u, BLE_GATT_DSC_CLT_CFG_UUID16)) {
            context->cccd_found = true;
            context->cccd_handle = descriptor->handle;
            ESP_LOGI(TAG_GATT, "FFE1 CCCD found on conn=%u handle=0x%04X",
                     conn_handle, descriptor->handle);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        context->current_characteristic++;
        start_next_descriptor_discovery(context);
        return 0;
    }
    ESP_LOGE(TAG_GATT, "Descriptor discovery failed conn=%u status=%u",
             conn_handle, error->status);
    finish_discovery(context, error->status);
    return 0;
}

static void start_service_discovery(connection_context_t *context)
{
    ble_device_status_t status;
    ble_registry_get_status(context->entry, &status);
    if (!status.connected) {
        ESP_LOGW(TAG_GATT, "Service discovery skipped: device disconnected");
        return;
    }
    memset(context->services, 0, sizeof(context->services));
    memset(context->characteristics, 0, sizeof(context->characteristics));
    memset(context->descriptors, 0, sizeof(context->descriptors));
    context->service_count = 0;
    context->characteristic_count = 0;
    context->descriptor_count = 0;
    context->ffe0_found = false;
    context->ffe1_found = false;
    context->ffe1_value_handle = 0;
    context->ffe1_properties = 0;
    context->cccd_found = false;
    context->cccd_handle = 0;
    context->using_cached_handles = false;
    ble_registry_mark_discovering(context->entry);
    int rc = ble_gattc_disc_all_svcs(status.conn_handle, service_discovered, context);
    if (rc != 0) {
        ESP_LOGE(TAG_GATT, "Cannot start service discovery: rc=%d", rc);
        finish_discovery(context, rc);
    }
}

static void start_next_characteristic_discovery(connection_context_t *context)
{
    if (context->current_service >= context->service_count) {
        context->current_characteristic = 0;
        start_next_descriptor_discovery(context);
        return;
    }
    const struct ble_gatt_svc *service = &context->services[context->current_service].service;
    int rc = ble_gattc_disc_all_chrs(context->entry->conn_handle,
                                     service->start_handle, service->end_handle,
                                     characteristic_discovered, context);
    if (rc != 0) {
        ESP_LOGE(TAG_GATT, "Cannot start characteristic discovery: rc=%d", rc);
        finish_discovery(context, rc);
    }
}

static uint16_t descriptor_end_handle(const connection_context_t *context, uint8_t index)
{
    const characteristic_record_t *current = &context->characteristics[index];
    for (uint8_t next = index + 1; next < context->characteristic_count; ++next) {
        if (context->characteristics[next].service_index == current->service_index) {
            return context->characteristics[next].characteristic.def_handle - 1;
        }
    }
    return context->services[current->service_index].service.end_handle;
}

static void start_next_descriptor_discovery(connection_context_t *context)
{
    while (context->current_characteristic < context->characteristic_count) {
        const struct ble_gatt_chr *characteristic =
            &context->characteristics[context->current_characteristic].characteristic;
        uint16_t end_handle = descriptor_end_handle(context, context->current_characteristic);
        if (end_handle > characteristic->val_handle) {
            int rc = ble_gattc_disc_all_dscs(context->entry->conn_handle,
                                             characteristic->val_handle, end_handle,
                                             descriptor_discovered, context);
            if (rc == 0) {
                return;
            }
            ESP_LOGW(TAG_GATT, "Descriptor discovery start failed value=0x%04X rc=%d",
                     characteristic->val_handle, rc);
        }
        context->current_characteristic++;
    }
    finish_discovery(context, 0);
}

static void dump_profile(const connection_context_t *context)
{
    char address[18];
    ble_registry_format_address(&context->entry->address, address, sizeof(address));
    ESP_LOGI(TAG_GATT, "========================================");
    ESP_LOGI(TAG_GATT, "SP624E GATT PROFILE address=%s RSSI=%d", address, context->entry->rssi);
    ESP_LOGI(TAG_GATT, "Services=%u Characteristics=%u Descriptors=%u",
             context->service_count, context->characteristic_count, context->descriptor_count);
    for (uint8_t i = 0; i < context->service_count; ++i) {
        char uuid[BLE_UUID_STR_LEN];
        const struct ble_gatt_svc *service = &context->services[i].service;
        uuid_string(&service->uuid.u, uuid);
        ESP_LOGI(TAG_GATT, "SERVICE %s start=0x%04X end=0x%04X",
                 uuid, service->start_handle, service->end_handle);
        for (uint8_t j = 0; j < context->characteristic_count; ++j) {
            const characteristic_record_t *record = &context->characteristics[j];
            if (record->service_index != i) {
                continue;
            }
            char chr_uuid[BLE_UUID_STR_LEN];
            char properties[80];
            uuid_string(&record->characteristic.uuid.u, chr_uuid);
            properties_string(record->characteristic.properties, properties, sizeof(properties));
            ESP_LOGI(TAG_GATT, "  CHARACTERISTIC %s def=0x%04X value=0x%04X [%s]",
                     chr_uuid, record->characteristic.def_handle,
                     record->characteristic.val_handle, properties);
            for (uint8_t k = 0; k < context->descriptor_count; ++k) {
                if (context->descriptors[k].characteristic_value_handle ==
                    record->characteristic.val_handle) {
                    char dsc_uuid[BLE_UUID_STR_LEN];
                    uuid_string(&context->descriptors[k].descriptor.uuid.u, dsc_uuid);
                    ESP_LOGI(TAG_GATT, "    DESCRIPTOR %s handle=0x%04X", dsc_uuid,
                             context->descriptors[k].descriptor.handle);
                }
            }
        }
    }
    ESP_LOGI(TAG_GATT, "FFE0=%s FFE1=%s FFE1_handle=0x%04X properties=0x%02X CCCD=%s handle=0x%04X",
             context->ffe0_found ? "FOUND" : "MISSING",
             context->ffe1_found ? "FOUND" : "MISSING",
             context->ffe1_value_handle, context->ffe1_properties,
             context->cccd_found ? "FOUND" : "MISSING", context->cccd_handle);
    ESP_LOGI(TAG_GATT, "========================================");
}

static void finish_discovery(connection_context_t *context, int status)
{
    if (context->discovery_done) {
        return;
    }
    context->discovery_done = true;
    if (status == 0) {
        dump_profile(context);
    }
    ble_registry_mark_gatt_result(context->entry, context->ffe0_found,
                                  context->ffe1_found, context->ffe1_value_handle,
                                  context->ffe1_properties);
    if (status != 0) {
        ble_registry_mark_error(context->entry);
    }
    char address[18];
    ble_registry_format_address(&context->entry->address, address, sizeof(address));
    if (context->ffe0_found && context->ffe1_found && status == 0) {
        ESP_LOGI(TAG_SP624E, "Device READY address=%s (signature + FFE0 + FFE1)", address);
    } else {
        ESP_LOGE(TAG_SP624E, "Device not READY address=%s status=%d FFE0=%d FFE1=%d",
                 address, status, context->ffe0_found, context->ffe1_found);
    }
    if (status == 0 && context->ffe0_found && context->ffe1_found && context->cccd_found) {
        context->cache_valid = true;
        context->cached_ffe1_value_handle = context->ffe1_value_handle;
        context->cached_ffe1_properties = context->ffe1_properties;
        context->cached_cccd_handle = context->cccd_handle;
        ble_connection_manager_on_gatt_ready(context->side);
    } else {
        ble_connection_manager_on_gatt_failed(context->side,
                                               status != 0 ? status : BLE_HS_EUNKNOWN);
    }
}

static int mtu_exchanged(uint16_t conn_handle, const struct ble_gatt_error *error,
                         uint16_t mtu, void *arg)
{
    connection_context_t *context = arg;
    ESP_LOGI(TAG_GATT, "MTU exchange conn=%u status=%u mtu=%u",
             conn_handle, error->status, mtu);
    ble_device_status_t status;
    ble_registry_get_status(context->entry, &status);
    if (status.connected && status.conn_handle == conn_handle) {
        start_service_discovery(context);
    }
    return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    connection_context_t *context = arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGE(TAG_CONNECT, "Connection failed status=0x%X (%s)",
                     event->connect.status, reason_name(event->connect.status));
            ble_registry_mark_error(context->entry);
            ble_connection_manager_on_connect_failed(context->side, event->connect.status);
            return 0;
        }
        ble_registry_mark_connected(context->entry, event->connect.conn_handle);
        ble_connection_manager_on_connected(context->side, event->connect.conn_handle);
        {
            char address[18];
            ble_registry_format_address(&context->entry->address, address, sizeof(address));
            ESP_LOGI(TAG_CONNECT, "CONNECTED address=%s handle=%u",
                      address, event->connect.conn_handle);
            struct ble_gap_conn_desc description;
            if (ble_gap_conn_find(event->connect.conn_handle, &description) == 0) {
                ESP_LOGI(TAG_CONNECT,
                         "%s params interval=%u (%.2fms) latency=%u supervision=%u (%.0fms)",
                         sp624e_side_name(context->side), description.conn_itvl,
                         description.conn_itvl * 1.25, description.conn_latency,
                         description.supervision_timeout,
                         description.supervision_timeout * 10.0);
            }
        }
        {
            struct ble_gap_conn_desc description;
            if (ble_gap_conn_find(event->connect.conn_handle, &description) == 0) {
                struct ble_gap_upd_params update = {
                    .itvl_min = description.conn_itvl,
                    .itvl_max = description.conn_itvl,
                    .latency = description.conn_latency,
                    .supervision_timeout = APP_BLE_REQUESTED_SUPERVISION_TIMEOUT_UNITS,
                    .min_ce_len = 0,
                    .max_ce_len = 0,
                };
                int update_rc = ble_gap_update_params(event->connect.conn_handle, &update);
                ESP_LOGI(TAG_CONNECT,
                         "%s requested supervision=%u (%.0fms) update_rc=%d",
                         sp624e_side_name(context->side),
                         APP_BLE_REQUESTED_SUPERVISION_TIMEOUT_UNITS,
                         APP_BLE_REQUESTED_SUPERVISION_TIMEOUT_UNITS * 10.0,
                         update_rc);
                ble_connection_manager_on_connection_params(
                    context->side,
                    APP_BLE_REQUESTED_SUPERVISION_TIMEOUT_UNITS * 10,
                    description.supervision_timeout * 10);
            }
        }
        if (context->cache_valid) {
            context->ffe0_found = true;
            context->ffe1_found = true;
            context->cccd_found = true;
            context->ffe1_value_handle = context->cached_ffe1_value_handle;
            context->ffe1_properties = context->cached_ffe1_properties;
            context->cccd_handle = context->cached_cccd_handle;
            context->discovery_done = true;
            context->using_cached_handles = true;
            ESP_LOGI(TAG_GATT,
                     "%s FAST GATT PATH FFE1=0x%04X CCCD=0x%04X; CCCD/query required",
                     sp624e_side_name(context->side), context->ffe1_value_handle,
                     context->cccd_handle);
            ble_connection_manager_on_gatt_ready(context->side);
        } else if (ble_gattc_exchange_mtu(event->connect.conn_handle,
                                          mtu_exchanged, context) != 0) {
            start_service_discovery(context);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
        connection_context_t *disconnected = context;
        ble_device_status_t prior_status;
        ble_registry_get_status(disconnected->entry, &prior_status);
        uint32_t uptime = prior_status.connected && prior_status.connected_at_us > 0 ?
            (uint32_t)((esp_timer_get_time() - prior_status.connected_at_us) / 1000000) : 0;
        char address[18];
        ble_registry_format_address(&disconnected->entry->address, address, sizeof(address));
        ble_registry_mark_disconnected(disconnected->entry, event->disconnect.reason);
        ESP_LOGW(TAG_CONNECT,
                 "DISCONNECTED address=%s handle=%u reason=0x%X (%s) uptime=%" PRIu32 "s",
                 address, event->disconnect.conn.conn_handle, event->disconnect.reason,
                 reason_name(event->disconnect.reason), uptime);
        ble_connection_manager_on_disconnected(disconnected->side,
                                               event->disconnect.reason);
        return 0;
    }

    case BLE_GAP_EVENT_NOTIFY_RX: {
        size_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
        uint8_t data[SP624E_MESSAGE_MAX_LEN + 3];
        if (length > sizeof(data)) {
            ESP_LOGE(TAG_GATT, "Notification oversized conn=%u attr=0x%04X len=%u",
                     event->notify_rx.conn_handle, event->notify_rx.attr_handle,
                     (unsigned)length);
            return 0;
        }
        int rc = os_mbuf_copydata(event->notify_rx.om, 0, length, data);
        if (rc != 0) {
            ESP_LOGE(TAG_GATT, "Notification copy failed rc=%d", rc);
            return 0;
        }
        if (length > 0 && event->notify_rx.attr_handle == context->ffe1_value_handle) {
            ble_connection_manager_on_ble_rx(context->side,
                                             esp_timer_get_time() / 1000);
        }
        sp624e_controller_on_notification(event->notify_rx.conn_handle,
                                          event->notify_rx.attr_handle, data, length);
        return 0;
    }

    case BLE_GAP_EVENT_CONN_UPDATE:
        {
            struct ble_gap_conn_desc description;
            uint16_t accepted_ms = 0;
            if (ble_gap_conn_find(event->conn_update.conn_handle, &description) == 0) {
                accepted_ms = description.supervision_timeout * 10;
            }
            ESP_LOGI(TAG_CONNECT,
                     "Connection update handle=%u status=%d requested=%ums accepted=%ums",
                     event->conn_update.conn_handle, event->conn_update.status,
                     APP_BLE_REQUESTED_SUPERVISION_TIMEOUT_UNITS * 10, accepted_ms);
            ble_connection_manager_on_connection_params(
                context->side,
                APP_BLE_REQUESTED_SUPERVISION_TIMEOUT_UNITS * 10,
                accepted_ms);
        }
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        if (event->conn_update_req.self_params != NULL &&
            event->conn_update_req.peer_params != NULL) {
            uint16_t peer_timeout =
                event->conn_update_req.peer_params->supervision_timeout;
            uint16_t response_timeout = ble_supervision_timeout_response(
                APP_BLE_REQUESTED_SUPERVISION_TIMEOUT_UNITS,
                peer_timeout, context->entry->rssi, APP_BLE_WEAK_RSSI_DBM);
            event->conn_update_req.self_params->supervision_timeout = response_timeout;
            ESP_LOGI(TAG_CONNECT,
                     "%s peer supervision request=%ums response=%ums RSSI=%d",
                     sp624e_side_name(context->side), peer_timeout * 10,
                     response_timeout * 10, context->entry->rssi);
        }
        return 0;

    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        if (event->conn_update_req.peer_params == NULL) return BLE_ERR_CONN_PARMS;
        return ble_supervision_timeout_response(
                   APP_BLE_REQUESTED_SUPERVISION_TIMEOUT_UNITS,
                   event->conn_update_req.peer_params->supervision_timeout,
                   context->entry->rssi, APP_BLE_WEAK_RSSI_DBM) ==
               event->conn_update_req.peer_params->supervision_timeout ?
               0 : BLE_ERR_CONN_PARMS;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG_GATT, "MTU event handle=%u channel=%u value=%u",
                 event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG_CONNECT, "Encryption change handle=%u status=%d",
                 event->enc_change.conn_handle, event->enc_change.status);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ESP_LOGW(TAG_CONNECT, "Repeat pairing ignored; no bonding required by this stage");
        return BLE_GAP_REPEAT_PAIRING_IGNORE;

    default:
        return 0;
    }
}

static int connect_context(connection_context_t *context, uint32_t timeout_ms)
{
    char address[18];
    ble_registry_format_address(&context->entry->address, address, sizeof(address));
    context->discovery_done = false;
    ble_registry_mark_connecting(context->entry);
    ESP_LOGI(TAG_CONNECT, "Connecting %s address=%s", sp624e_side_name(context->side),
             address);
    const struct ble_gap_conn_params params = {
        .scan_itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN,
        .scan_window = BLE_GAP_SCAN_FAST_WINDOW,
        .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
        .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
        .latency = BLE_GAP_INITIAL_CONN_LATENCY,
        .supervision_timeout = APP_BLE_INITIAL_SUPERVISION_TIMEOUT_UNITS,
        .min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN,
        .max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN,
    };
    int rc = ble_gap_connect(s_own_addr_type, &context->entry->address,
                              timeout_ms, &params, gap_event, context);
    if (rc != 0) {
        ESP_LOGE(TAG_CONNECT, "Connection start failed address=%s rc=%d", address, rc);
        ble_registry_mark_error(context->entry);
    }
    return rc;
}

void ble_connection_start_two(uint8_t own_addr_type,
                              ble_device_entry_t *first,
                              ble_device_entry_t *second)
{
    memset(s_contexts, 0, sizeof(s_contexts));
    sp624e_mapping_t mapping;
    bool ordered = false;
    if (sp624e_mapping_load(&mapping) == ESP_OK && mapping.valid) {
        sp624e_address_t first_address = {.type = first->address.type};
        memcpy(first_address.val, first->address.val, sizeof(first_address.val));
        if (sp624e_address_equal(&mapping.left, &first_address)) {
            s_contexts[0].entry = first;
            s_contexts[1].entry = second;
            ordered = true;
        } else {
            sp624e_address_t second_address = {.type = second->address.type};
            memcpy(second_address.val, second->address.val, sizeof(second_address.val));
            if (sp624e_address_equal(&mapping.left, &second_address)) {
                s_contexts[0].entry = second;
                s_contexts[1].entry = first;
                ordered = true;
            }
        }
    }
    if (!ordered) {
        s_contexts[0].entry = first;
        s_contexts[1].entry = second;
        ESP_LOGW(TAG_CONNECT, "No usable persisted mapping; temporary scan order used");
    }
    s_contexts[0].side = SP624E_SIDE_LEFT;
    s_contexts[1].side = SP624E_SIDE_RIGHT;
    s_own_addr_type = own_addr_type;
    ESP_LOGI(TAG_SP624E, "SP624E writes: QUEUED + VERIFIED; strict group mode enabled");
    ble_connection_manager_start();
}

int ble_connection_connect(sp624e_side_t side, uint32_t timeout_ms)
{
    if (side >= SP624E_SIDE_COUNT || s_contexts[side].entry == NULL) return BLE_HS_EINVAL;
    return connect_context(&s_contexts[side], timeout_ms);
}

int ble_connection_cancel_connect(sp624e_side_t side)
{
    if (side >= SP624E_SIDE_COUNT || s_contexts[side].entry == NULL) return BLE_HS_EINVAL;
    int rc = ble_gap_conn_cancel();
    ESP_LOGW(TAG_CONNECT, "%s connection attempt cancel rc=%d",
             sp624e_side_name(side), rc);
    return rc;
}

int ble_connection_start_full_discovery(sp624e_side_t side)
{
    if (side >= SP624E_SIDE_COUNT || s_contexts[side].entry == NULL) return BLE_HS_EINVAL;
    connection_context_t *context = &s_contexts[side];
    ble_device_status_t status;
    ble_registry_get_status(context->entry, &status);
    if (!status.connected) return BLE_HS_ENOTCONN;
    context->cache_valid = false;
    context->using_cached_handles = false;
    context->discovery_done = false;
    start_service_discovery(context);
    return 0;
}

int ble_connection_terminate(sp624e_side_t side, uint8_t reason)
{
    if (side >= SP624E_SIDE_COUNT || s_contexts[side].entry == NULL) return BLE_HS_EINVAL;
    ble_device_status_t status;
    ble_registry_get_status(s_contexts[side].entry, &status);
    if (!status.connected) return BLE_HS_ENOTCONN;
    return ble_gap_terminate(status.conn_handle, reason);
}

void ble_connection_force_cleanup(sp624e_side_t side, int reason)
{
    if (side >= SP624E_SIDE_COUNT || s_contexts[side].entry == NULL) return;
    connection_context_t *context = &s_contexts[side];
    context->discovery_done = false;
    context->ffe0_found = false;
    context->ffe1_found = false;
    context->cccd_found = false;
    context->ffe1_value_handle = 0;
    context->cccd_handle = 0;
    context->using_cached_handles = false;
    ble_registry_mark_disconnected(context->entry, reason);
}

bool ble_connection_get_transport(sp624e_side_t side, sp624e_transport_t *transport)
{
    if (side >= SP624E_SIDE_COUNT || transport == NULL || s_contexts[side].entry == NULL ||
        !s_contexts[side].discovery_done || !s_contexts[side].ffe0_found ||
        !s_contexts[side].ffe1_found || !s_contexts[side].cccd_found) return false;
    *transport = controller_transport(&s_contexts[side]);
    return true;
}

const ble_addr_t *ble_connection_get_address(sp624e_side_t side)
{
    if (side >= SP624E_SIDE_COUNT || s_contexts[side].entry == NULL) return NULL;
    return &s_contexts[side].entry->address;
}

int8_t ble_connection_get_rssi(sp624e_side_t side)
{
    if (side >= SP624E_SIDE_COUNT || s_contexts[side].entry == NULL) return 0;
    return s_contexts[side].entry->rssi;
}

bool ble_connection_was_observed(sp624e_side_t side)
{
    if (side >= SP624E_SIDE_COUNT || s_contexts[side].entry == NULL) return false;
    const ble_device_entry_t *entry = s_contexts[side].entry;
    return entry->manufacturer_present || entry->raw_adv_len > 0 ||
           entry->raw_scan_rsp_len > 0;
}
