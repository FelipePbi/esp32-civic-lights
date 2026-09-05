#include "interior_light.h"

#include <inttypes.h>
#include <string.h>

#include "app_config.h"
#include "ble/ble_connection_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "nimble/ble.h"

static const char *TAG = "INTERIOR";

/*
 * All state transitions happen inside interior_light_service(), which runs on a
 * single existing task. NimBLE callbacks only publish small scalar results, so
 * a short critical section is enough and no mutex is required. NimBLE is never
 * called from inside the critical section.
 */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool s_initialized;
static interior_light_state_t s_state;
static bool s_headlight_links_up;

static interior_rgb_t s_desired;
static interior_rgb_t s_last_attempted;
static bool s_last_attempt_valid;

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_value_handle;
static uint16_t s_service_start;
static uint16_t s_service_end;

/* Published by NimBLE callbacks, consumed by the service loop. */
static volatile bool s_master_yield;
static volatile bool s_expected_disconnect;
static volatile bool s_connect_result_ready;
static volatile int s_connect_status;
static volatile bool s_disconnect_event;
static volatile int s_disconnect_reason;
static volatile bool s_service_found;
static volatile bool s_service_done;
static volatile int s_service_status;
static volatile bool s_chr_found;
static volatile bool s_chr_done;
static volatile int s_chr_status;

static int64_t s_deadline_ms;
static int64_t s_last_activity_ms;
static int64_t s_last_write_ms;
static int64_t s_backoff_until_ms;
static uint32_t s_backoff_ms;
static uint32_t s_write_attempts;
static uint32_t s_connect_attempts;
static uint32_t s_connect_failures;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static const uint8_t s_address_bytes[6] = APP_INTERIOR_LIGHT_ADDR_BYTES;

static ble_addr_t interior_address(void)
{
    ble_addr_t address = {.type = APP_INTERIOR_LIGHT_ADDR_TYPE};
    memcpy(address.val, s_address_bytes, sizeof(address.val));
    return address;
}

const char *interior_light_state_name(interior_light_state_t state)
{
    switch (state) {
    case INTERIOR_LIGHT_IDLE: return "IDLE";
    case INTERIOR_LIGHT_PENDING: return "PENDING";
    case INTERIOR_LIGHT_CONNECTING: return "CONNECTING";
    case INTERIOR_LIGHT_DISCOVERING: return "DISCOVERING";
    case INTERIOR_LIGHT_CONNECTED: return "CONNECTED";
    case INTERIOR_LIGHT_BACKOFF: return "BACKOFF";
    default: return "INVALID";
    }
}

/* ------------------------------------------------------------------ */
/* Intent                                                              */
/* ------------------------------------------------------------------ */

void interior_light_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    portENTER_CRITICAL(&s_mux);
    s_desired.red = red;
    s_desired.green = green;
    s_desired.blue = blue;
    portEXIT_CRITICAL(&s_mux);
}

void interior_light_set_off(void) { interior_light_set_color(0, 0, 0); }

void interior_light_follow_desired(const sp624e_desired_state_t *desired)
{
    interior_rgb_t mapped = interior_light_map_desired(desired);
    interior_light_set_color(mapped.red, mapped.green, mapped.blue);
}

bool interior_light_is_connected(void)
{
    return s_state == INTERIOR_LIGHT_CONNECTED;
}

void interior_light_get_snapshot(interior_light_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    memset(snapshot, 0, sizeof(*snapshot));
    portENTER_CRITICAL(&s_mux);
    snapshot->state = s_state;
    snapshot->connected = s_state == INTERIOR_LIGHT_CONNECTED;
    snapshot->desired = s_desired;
    snapshot->last_attempted = s_last_attempted;
    snapshot->last_attempt_valid = s_last_attempt_valid;
    snapshot->last_write_ms = s_last_write_ms;
    snapshot->write_attempts = s_write_attempts;
    snapshot->connect_attempts = s_connect_attempts;
    snapshot->connect_failures = s_connect_failures;
    snapshot->backoff_ms = s_backoff_ms;
    snapshot->last_disconnect_reason = s_disconnect_reason;
    portEXIT_CRITICAL(&s_mux);
}

/* ------------------------------------------------------------------ */
/* NimBLE callbacks: publish results only, no logic                    */
/* ------------------------------------------------------------------ */

static int interior_service_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                               const struct ble_gatt_svc *service, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0 && service != NULL) {
        s_service_start = service->start_handle;
        s_service_end = service->end_handle;
        s_service_found = true;
        return 0;
    }
    s_service_status = error->status == BLE_HS_EDONE ? 0 : error->status;
    s_service_done = true;
    return 0;
}

static int interior_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0 && chr != NULL) {
        s_value_handle = chr->val_handle;
        s_chr_found = true;
        return 0;
    }
    s_chr_status = error->status == BLE_HS_EDONE ? 0 : error->status;
    s_chr_done = true;
    return 0;
}

static int interior_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_connect_status = event->connect.status;
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
        }
        s_connect_result_ready = true;
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_disconnect_reason = event->disconnect.reason;
        s_disconnect_event = true;
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* State machine helpers                                               */
/* ------------------------------------------------------------------ */

static void clear_link_state(void)
{
    portENTER_CRITICAL(&s_mux);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_value_handle = 0;
    s_service_start = 0;
    s_service_end = 0;
    portEXIT_CRITICAL(&s_mux);
    s_connect_result_ready = false;
    s_service_found = false;
    s_service_done = false;
    s_chr_found = false;
    s_chr_done = false;
}

static void enter_backoff(const char *reason)
{
    uint32_t next = s_backoff_ms == 0 ? APP_INTERIOR_LIGHT_BACKOFF_MIN_MS
                                      : s_backoff_ms * 2;
    if (next > APP_INTERIOR_LIGHT_BACKOFF_MAX_MS) next = APP_INTERIOR_LIGHT_BACKOFF_MAX_MS;
    s_backoff_ms = next;
    s_backoff_until_ms = now_ms() + next;
    s_state = INTERIOR_LIGHT_BACKOFF;
    ESP_LOGW(TAG, "%s; retry in %" PRIu32 " ms", reason, next);
}

/* True when a colour still has to reach the controller. */
static bool write_pending(void)
{
    interior_rgb_t desired;
    interior_rgb_t attempted;
    bool valid;
    portENTER_CRITICAL(&s_mux);
    desired = s_desired;
    attempted = s_last_attempted;
    valid = s_last_attempt_valid;
    portEXIT_CRITICAL(&s_mux);
    return !valid || interior_rgb_differs(desired, attempted);
}

/*
 * SP624E always win the radio. A connect attempt needs the BLE master role, so
 * it only starts while both headlights are READY, which is exactly when the
 * connection manager is neither scanning nor connecting.
 */
static bool safe_to_use_master(void)
{
    return ble_connection_manager_both_ready();
}

/*
 * Both SP624E links physically up. Unlike both_ready(), this ignores the
 * RECONCILING transitions a normal colour change goes through: it only drops
 * when a link is actually lost.
 */
static bool both_links_up(void)
{
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        ble_connection_manager_status_t status;
        ble_connection_manager_get_status(side, &status);
        if (!status.connected) return false;
    }
    return true;
}

/*
 * Runs on the connection manager task, not on the service task. It only frees
 * the radio synchronously and raises a flag; the state machine transition is
 * left to interior_light_service() so s_state has a single writer.
 */
void interior_light_release_master(const char *reason)
{
    (void)reason;
    if (!s_initialized || s_state != INTERIOR_LIGHT_CONNECTING) return;
    s_master_yield = true;
    (void)ble_gap_conn_cancel();
}

static void start_connect(void)
{
    uint8_t own_addr_type = 0;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        enter_backoff("no usable BLE identity address");
        return;
    }
    clear_link_state();
    ble_addr_t address = interior_address();
    s_connect_attempts++;
    ESP_LOGI(TAG, "connect requested");
    int rc = ble_gap_connect(own_addr_type, &address, APP_INTERIOR_LIGHT_CONNECT_TIMEOUT_MS,
                             NULL, interior_gap_event, NULL);
    if (rc != 0) {
        s_connect_failures++;
        enter_backoff("connect start failed");
        return;
    }
    s_state = INTERIOR_LIGHT_CONNECTING;
    s_deadline_ms = now_ms() + APP_INTERIOR_LIGHT_CONNECT_TIMEOUT_MS +
                    APP_INTERIOR_LIGHT_STEP_GRACE_MS;
}

static void start_service_discovery(void)
{
    ble_uuid16_t service_uuid = BLE_UUID16_INIT(APP_INTERIOR_LIGHT_SERVICE_UUID16);
    s_service_found = false;
    s_service_done = false;
    s_service_status = 0;
    int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &service_uuid.u,
                                        interior_service_cb, NULL);
    if (rc != 0) {
        (void)ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        enter_backoff("service discovery start failed");
        return;
    }
    s_state = INTERIOR_LIGHT_DISCOVERING;
    s_deadline_ms = now_ms() + APP_INTERIOR_LIGHT_DISCOVERY_TIMEOUT_MS;
}

static void start_characteristic_discovery(void)
{
    ble_uuid16_t chr_uuid = BLE_UUID16_INIT(APP_INTERIOR_LIGHT_CHARACTERISTIC_UUID16);
    s_chr_found = false;
    s_chr_done = false;
    s_chr_status = 0;
    int rc = ble_gattc_disc_chrs_by_uuid(s_conn_handle, s_service_start, s_service_end,
                                         &chr_uuid.u, interior_chr_cb, NULL);
    if (rc != 0) {
        (void)ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        enter_backoff("characteristic discovery start failed");
    }
}

static void write_desired(void)
{
    interior_rgb_t desired;
    portENTER_CRITICAL(&s_mux);
    desired = s_desired;
    portEXIT_CRITICAL(&s_mux);

    uint8_t frame[INTERIOR_LIGHT_FRAME_LEN];
    interior_light_build_frame(desired, frame);
    int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, s_value_handle, frame,
                                         sizeof(frame));
    int64_t now = now_ms();
    if (rc != 0) {
        ESP_LOGW(TAG, "write failed rc=%d RGB=%u,%u,%u", rc, desired.red, desired.green,
                 desired.blue);
        /* Keep the intent pending; the next service pass retries. */
        s_last_activity_ms = now;
        return;
    }
    portENTER_CRITICAL(&s_mux);
    s_last_attempted = desired;
    s_last_attempt_valid = true;
    s_last_write_ms = now;
    s_write_attempts++;
    portEXIT_CRITICAL(&s_mux);
    s_last_activity_ms = now;
    /* No acknowledgement exists on this controller: this is an attempt, not a
       confirmation of the physical colour. */
    ESP_LOGI(TAG, "write attempted RGB=%u,%u,%u", desired.red, desired.green,
             desired.blue);
}

/* ------------------------------------------------------------------ */
/* Service loop                                                        */
/* ------------------------------------------------------------------ */

void interior_light_service(void)
{
    if (!s_initialized) return;
    int64_t now = now_ms();

    /*
     * The headlights and the interior kit share the car's power. When both
     * SP624E links come back after a loss, the LEDCAR very likely lost its
     * colour too, so the intent is reasserted even if it never changed. Without
     * this, an interior that was already idle-disconnected at the moment of a
     * power cycle would silently keep whatever colour the controller booted in.
     */
    bool links_up = both_links_up();
    if (links_up && !s_headlight_links_up) {
        portENTER_CRITICAL(&s_mux);
        bool had_attempt = s_last_attempt_valid;
        s_last_attempt_valid = false;
        portEXIT_CRITICAL(&s_mux);
        if (had_attempt) ESP_LOGI(TAG, "headlight links recovered; will reapply intent");
    }
    s_headlight_links_up = links_up;

    if (s_master_yield) {
        s_master_yield = false;
        if (s_state == INTERIOR_LIGHT_CONNECTING) {
            s_connect_failures++;
            clear_link_state();
            enter_backoff("SP624E recovery needs the BLE master role");
        }
    }

    if (s_disconnect_event) {
        s_disconnect_event = false;
        int reason = s_disconnect_reason;
        bool expected = s_expected_disconnect;
        s_expected_disconnect = false;
        clear_link_state();
        if (!expected) {
            /* The controller may have been power-cycled with the car and lost
               its colour, so the intent has to be applied again. */
            portENTER_CRITICAL(&s_mux);
            s_last_attempt_valid = false;
            portEXIT_CRITICAL(&s_mux);
            ESP_LOGI(TAG, "disconnected reason=0x%X; will reapply intent", reason);
        }
        if (s_state != INTERIOR_LIGHT_BACKOFF) s_state = INTERIOR_LIGHT_IDLE;
    }

    switch (s_state) {
    case INTERIOR_LIGHT_IDLE:
        if (write_pending()) s_state = INTERIOR_LIGHT_PENDING;
        break;

    case INTERIOR_LIGHT_PENDING:
        if (!write_pending()) {
            s_state = INTERIOR_LIGHT_IDLE;
            break;
        }
        if (safe_to_use_master()) start_connect();
        break;

    case INTERIOR_LIGHT_CONNECTING:
        if (s_connect_result_ready) {
            s_connect_result_ready = false;
            if (s_connect_status != 0) {
                s_connect_failures++;
                clear_link_state();
                enter_backoff("connect failed");
                break;
            }
            ESP_LOGI(TAG, "connected");
            s_last_activity_ms = now;
            start_service_discovery();
            break;
        }
        if (now >= s_deadline_ms) {
            (void)ble_gap_conn_cancel();
            s_connect_failures++;
            clear_link_state();
            enter_backoff("connect timeout");
        }
        break;

    case INTERIOR_LIGHT_DISCOVERING:
        if (s_service_done && !s_chr_done) {
            if (s_service_status != 0 || !s_service_found) {
                (void)ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                enter_backoff("FFE0 service not found");
                break;
            }
            s_service_done = false;
            start_characteristic_discovery();
            break;
        }
        if (s_chr_done) {
            s_chr_done = false;
            if (s_chr_status != 0 || !s_chr_found || s_value_handle == 0) {
                (void)ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                enter_backoff("FFE1 characteristic not found");
                break;
            }
            ESP_LOGI(TAG, "GATT ready handle=0x%04X", s_value_handle);
            s_backoff_ms = 0;
            s_state = INTERIOR_LIGHT_CONNECTED;
            s_last_activity_ms = now;
            break;
        }
        if (now >= s_deadline_ms) {
            (void)ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            enter_backoff("discovery timeout");
        }
        break;

    case INTERIOR_LIGHT_CONNECTED:
        if (write_pending() &&
            now - s_last_write_ms >= APP_INTERIOR_LIGHT_MIN_WRITE_INTERVAL_MS) {
            write_desired();
            break;
        }
        if (!write_pending() &&
            now - s_last_activity_ms >= APP_INTERIOR_LIGHT_IDLE_DISCONNECT_MS) {
            ESP_LOGI(TAG, "idle timeout, disconnect");
            s_expected_disconnect = true;
            (void)ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            s_state = INTERIOR_LIGHT_IDLE;
        }
        break;

    case INTERIOR_LIGHT_BACKOFF:
        if (now >= s_backoff_until_ms) s_state = INTERIOR_LIGHT_IDLE;
        break;

    default:
        s_state = INTERIOR_LIGHT_IDLE;
        break;
    }
}

esp_err_t interior_light_init(void)
{
    if (s_initialized) return ESP_OK;
    s_state = INTERIOR_LIGHT_IDLE;
    s_desired = (interior_rgb_t){0, 0, 0};
    s_last_attempted = (interior_rgb_t){0, 0, 0};
    /* Unknown physical colour at boot: force one write of the mapped intent. */
    s_last_attempt_valid = false;
    s_headlight_links_up = false;
    clear_link_state();
    s_initialized = true;
    ESP_LOGI(TAG, "interior light ready (best effort, no observed state)");
    return ESP_OK;
}
