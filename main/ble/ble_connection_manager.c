#include "ble_connection_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "ble_backoff.h"
#include "ble_recovery_policy.h"
#include "ble_scanner.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nimble/hci_common.h"
#include "remote/rf_remote.h"
#include "sp624e/sp624e_controller.h"

#define MANAGER_EVENT_DEPTH 32
#define MANAGER_TICK_MS 100
#define READY_BACKOFF_RESET_MS 10000
#define DISCOVERY_TIMEOUT_MS 10000
#define PIPELINE_TIMEOUT_MS 6000
#define TERMINATION_TIMEOUT_MS 2000
#define STABLE_LINK_EVIDENCE_MS 10000
#define DUAL_DISCONNECT_WINDOW_MS 500
#define OPERATIONAL_HEARTBEAT_MAX_AGE_MS 2000

static const char *TAG = "CONN_MANAGER";

typedef enum {
    EVENT_START = 0,
    EVENT_CONNECTED,
    EVENT_CONNECT_FAILED,
    EVENT_GATT_READY,
    EVENT_GATT_FAILED,
    EVENT_DISCONNECTED,
    EVENT_ADV_FOUND,
    EVENT_STAGE,
    EVENT_UNHEALTHY,
    EVENT_MANUAL_DISCONNECT,
    EVENT_MANUAL_RECONNECT,
    EVENT_POWER_CYCLE_TEST,
    EVENT_BLE_RX,
    EVENT_VALID_STATE,
    EVENT_GROUP_SYNCED,
    EVENT_CONNECTION_PARAMS,
    EVENT_FAST_GATT_FAILED,
} manager_event_type_t;

typedef struct {
    manager_event_type_t type;
    sp624e_side_t side;
    int reason;
    uint16_t conn_handle;
    int8_t rssi;
    ble_connection_state_t state;
    uint16_t requested_ms;
    uint16_t accepted_ms;
    int64_t timestamp_ms;
    char text[48];
} manager_event_t;

typedef struct {
    ble_connection_manager_status_t status;
    bool ever_ready;
    bool initial_attempted;
    bool advertisement_seen;
    int64_t action_due_ms;
    int64_t connect_deadline_ms;
    int64_t recovery_due_ms;
    int recovery_reason;
    bool manual_disconnect;
    bool force_fast_test;
    bool connect_cancel_pending;
    bool using_cached_gatt;
    bool recovery_ready_recorded;
    ble_recovery_window_t recovery_window;
    uint32_t detection_samples;
    uint32_t adv_connect_samples;
    uint32_t adv_ready_samples;
    uint32_t disconnect_synced_samples;
    sp624e_connection_metrics_t metrics;
} side_context_t;

static side_context_t s_sides[SP624E_SIDE_COUNT];
static StaticQueue_t s_event_queue_storage;
static uint8_t s_event_queue_buffer[MANAGER_EVENT_DEPTH * sizeof(manager_event_t)];
static QueueHandle_t s_event_queue;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static bool s_started;
static int64_t s_controller_started_ms;
static uint8_t s_scan_mask;
static int s_connect_side = -1;
static portMUX_TYPE s_critical_mux = portMUX_INITIALIZER_UNLOCKED;
static manager_event_t s_critical_events[SP624E_SIDE_COUNT];
static bool s_critical_pending[SP624E_SIDE_COUNT];
static uint32_t s_critical_replacements[SP624E_SIDE_COUNT];
static int64_t s_last_ble_rx_ms[SP624E_SIDE_COUNT];
static int64_t s_last_valid_state_ms[SP624E_SIDE_COUNT];
static int64_t s_heartbeat_ms;
static ble_master_guard_fn s_master_guards[BLE_MASTER_GUARD_MAX];
static size_t s_master_guard_count;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

const char *ble_connection_state_name(ble_connection_state_t state)
{
    switch (state) {
    case BLE_CONNECTION_UNKNOWN: return "UNKNOWN";
    case BLE_CONNECTION_DISCONNECTED: return "DISCONNECTED";
    case BLE_CONNECTION_WAITING_FOR_ADV: return "WAITING_FOR_ADV";
    case BLE_CONNECTION_CONNECTING: return "CONNECTING";
    case BLE_CONNECTION_CONNECTED: return "CONNECTED";
    case BLE_CONNECTION_DISCOVERING: return "DISCOVERING";
    case BLE_CONNECTION_SUBSCRIBING: return "SUBSCRIBING";
    case BLE_CONNECTION_QUERYING_STATE: return "QUERYING_STATE";
    case BLE_CONNECTION_SYNC_PENDING: return "SYNC_PENDING";
    case BLE_CONNECTION_RECONCILING: return "RECONCILING";
    case BLE_CONNECTION_READY: return "READY";
    case BLE_CONNECTION_BACKOFF: return "BACKOFF";
    case BLE_CONNECTION_FAST_RECOVERY: return "FAST_RECOVERY";
    case BLE_CONNECTION_RECOVERING: return "RECOVERING";
    case BLE_CONNECTION_ERROR: return "ERROR";
    default: return "INVALID";
    }
}

static int critical_priority(manager_event_type_t type)
{
    switch (type) {
    case EVENT_DISCONNECTED: return 4;
    case EVENT_UNHEALTHY:
    case EVENT_GATT_FAILED: return 3;
    case EVENT_FAST_GATT_FAILED: return 3;
    case EVENT_CONNECT_FAILED: return 2;
    default: return 0;
    }
}

static void post_event(const manager_event_t *event)
{
    if (!s_started || event == NULL || event->side >= SP624E_SIDE_COUNT) return;
    int priority = critical_priority(event->type);
    if (priority > 0) {
        portENTER_CRITICAL(&s_critical_mux);
        manager_event_t *pending = &s_critical_events[event->side];
        if (!s_critical_pending[event->side] ||
            priority >= critical_priority(pending->type)) {
            if (s_critical_pending[event->side]) {
                s_critical_replacements[event->side]++;
            }
            *pending = *event;
            s_critical_pending[event->side] = true;
        }
        portEXIT_CRITICAL(&s_critical_mux);
        return;
    }
    if (xQueueSend(s_event_queue, event, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Event queue full type=%d side=%s", event->type,
                 sp624e_side_name(event->side));
    }
}

static bool take_critical_event(manager_event_t *event)
{
    bool found = false;
    portENTER_CRITICAL(&s_critical_mux);
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        if (!s_critical_pending[side]) continue;
        *event = s_critical_events[side];
        s_critical_pending[side] = false;
        found = true;
        break;
    }
    portEXIT_CRITICAL(&s_critical_mux);
    return found;
}

static void transition(sp624e_side_t side, ble_connection_state_t state,
                       const char *reason)
{
    side_context_t *context = &s_sides[side];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ble_connection_state_t previous = context->status.state;
    if (previous == state && state != BLE_CONNECTION_READY) {
        xSemaphoreGive(s_lock);
        return;
    }
    context->status.state = state;
    context->status.state_entered_ms = now_ms();
    if (state == BLE_CONNECTION_READY) {
        context->status.ready_since_ms = context->status.state_entered_ms;
    } else {
        context->status.ready_since_ms = 0;
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%s %s -> %s reason=%s", sp624e_side_name(side),
             ble_connection_state_name(previous), ble_connection_state_name(state),
             reason != NULL ? reason : "unspecified");
}

static uint32_t backoff_for(uint32_t attempt)
{
    uint32_t base = ble_backoff_base_ms(attempt);
    int jitter_percent = (int)(esp_random() % 21) - 10;
    return ble_backoff_with_jitter(base, jitter_percent);
}

static void update_average(uint32_t sample, uint32_t *count,
                           uint32_t *average, uint32_t *maximum)
{
    (*count)++;
    *average = (uint32_t)((((uint64_t)*average * (*count - 1)) + sample) / *count);
    if (sample > *maximum) *maximum = sample;
}

static void reset_recovery_trace(side_context_t *context, int64_t disconnected_at)
{
    context->recovery_ready_recorded = false;
    context->metrics.recovery_started_ms = disconnected_at;
    context->metrics.scan_started_ms = 0;
    context->metrics.first_adv_ms = 0;
    context->metrics.connect_started_ms = 0;
    context->metrics.connected_ms = 0;
    context->metrics.gatt_ready_ms = 0;
    context->metrics.cccd_subscribed_ms = 0;
    context->metrics.state_valid_ms = 0;
    context->metrics.reconcile_started_ms = 0;
    context->metrics.group_synced_ms = 0;
    context->metrics.last_adv_after_loss_ms = 0;
    context->metrics.last_recovery_duration_ms = 0;
}

static bool recovery_platform_operational(int64_t now)
{
    rf_remote_snapshot_t rf = {0};
    rf_remote_get_snapshot(&rf);
    wifi_mode_t mode = WIFI_MODE_NULL;
    bool wifi_operational = esp_wifi_get_mode(&mode) == ESP_OK &&
        (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    bool rf_operational = rf.initialized && rf.heartbeat_ms > 0 &&
        now >= rf.heartbeat_ms &&
        now - rf.heartbeat_ms <= OPERATIONAL_HEARTBEAT_MAX_AGE_MS;
    return wifi_operational && rf_operational;
}

static void log_power_cycle_trace(sp624e_side_t side, const side_context_t *context)
{
    const sp624e_connection_metrics_t *m = &context->metrics;
    uint32_t detection = m->last_ble_rx_ms > 0 &&
                         m->last_disconnect_timestamp_ms >= m->last_ble_rx_ms ?
        (uint32_t)(m->last_disconnect_timestamp_ms - m->last_ble_rx_ms) : 0;
    uint32_t after_adv = m->first_adv_ms > 0 && m->group_synced_ms >= m->first_adv_ms ?
        (uint32_t)(m->group_synced_ms - m->first_adv_ms) : 0;
    ESP_LOGI(TAG, "POWER-CYCLE TRACE -----------------");
    ESP_LOGI(TAG, "side: %s classification=%s", sp624e_side_name(side),
             ble_disconnect_classification_name(m->last_disconnect_classification));
    ESP_LOGI(TAG, "last_ble_rx:      %" PRId64 " ms", m->last_ble_rx_ms);
    ESP_LOGI(TAG, "last_state_query: %" PRId64 " ms", m->last_state_query_valid_ms);
    ESP_LOGI(TAG, "disconnect_evt:   %" PRId64 " ms reason=0x%X",
             m->last_disconnect_timestamp_ms, m->last_disconnect_reason);
    ESP_LOGI(TAG, "recovery_started: %" PRId64 " ms scan_started=%" PRId64 " ms",
             m->recovery_started_ms, m->scan_started_ms);
    ESP_LOGI(TAG, "first_adv:        %" PRId64 " ms connect_started=%" PRId64 " ms",
             m->first_adv_ms, m->connect_started_ms);
    ESP_LOGI(TAG, "connected:        %" PRId64 " ms gatt_ready=%" PRId64 " ms",
             m->connected_ms, m->gatt_ready_ms);
    ESP_LOGI(TAG, "notify_ready:     %" PRId64 " ms state_valid=%" PRId64 " ms",
             m->cccd_subscribed_ms, m->state_valid_ms);
    ESP_LOGI(TAG, "reconcile:        %" PRId64 " ms synced=%" PRId64 " ms",
             m->reconcile_started_ms, m->group_synced_ms);
    ESP_LOGI(TAG, "Detection latency: %" PRIu32 " ms Recovery after ADV: %" PRIu32
                  " ms Total recovery: %" PRIu32 " ms",
             detection, after_adv, m->last_recovery_duration_ms);
}

static void enter_backoff(sp624e_side_t side, const char *reason, bool failure)
{
    side_context_t *context = &s_sides[side];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    context->status.reconnect_attempt++;
    context->status.current_backoff_ms = backoff_for(context->status.reconnect_attempt);
    uint32_t attempt = context->status.reconnect_attempt;
    uint32_t backoff = context->status.current_backoff_ms;
    xSemaphoreGive(s_lock);
    context->metrics.current_backoff_ms = backoff;
    if (failure) context->metrics.reconnect_failure_count++;
    sp624e_metrics_set_side(side, &context->metrics);
    context->action_due_ms = now_ms() + backoff;
    transition(side, BLE_CONNECTION_BACKOFF, reason);
    ESP_LOGW(TAG, "%s reconnect attempt=%" PRIu32 " backoff=%" PRIu32 "ms",
             sp624e_side_name(side), attempt, backoff);
}

static void enter_absent_backoff(sp624e_side_t side, const char *reason)
{
    side_context_t *context = &s_sides[side];
    uint32_t backoff = ble_backoff_absent_retry_ms();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    context->status.current_backoff_ms = backoff;
    uint32_t failure_attempt = context->status.reconnect_attempt;
    xSemaphoreGive(s_lock);
    context->metrics.current_backoff_ms = backoff;
    sp624e_metrics_set_side(side, &context->metrics);
    context->action_due_ms = now_ms() + backoff;
    transition(side, BLE_CONNECTION_BACKOFF, reason);
    ESP_LOGI(TAG, "%s peripheral absent; retry=%" PRIu32
                  "ms failure_attempt=%" PRIu32,
             sp624e_side_name(side), backoff, failure_attempt);
}

/*
 * Cancels the shared recovery scan and returns every OTHER side that was being
 * scanned for to BACKOFF.
 *
 * The scan targets both sides at once, but any of the paths below cancels it as
 * a whole: an advertisement from one side, a connect attempt, a disconnect or a
 * forced recovery. Without this, the side that was not the reason for the
 * cancellation stays in WAITING_FOR_ADV forever: nothing times that state out,
 * and begin_recovery_scan() only considers sides in BACKOFF. Observed on the
 * car as "one headlight never comes back after a power cycle until the ESP32 is
 * restarted".
 */
static void release_recovery_scan(int keep, const char *reason)
{
    if (s_scan_mask == 0) return;
    uint8_t mask = s_scan_mask;
    s_scan_mask = 0;
    ble_scanner_stop_scan();
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        if ((int)side == keep) continue;
        if ((mask & (uint8_t)(1U << side)) == 0) continue;
        side_context_t *context = &s_sides[side];
        if (context->status.connected) continue;
        if (context->status.state != BLE_CONNECTION_WAITING_FOR_ADV) continue;
        enter_absent_backoff(side, reason);
    }
}


static void enter_fast_recovery(sp624e_side_t side, const char *reason)
{
    side_context_t *context = &s_sides[side];
    int64_t now = now_ms();
    ble_recovery_window_start(&context->recovery_window,
                              BLE_DISCONNECT_LIKELY_POWER_CYCLE, now,
                              APP_BLE_FAST_RECOVERY_WINDOW_MS);
    context->metrics.fast_recovery_count++;
    context->metrics.fast_recovery_status = BLE_FAST_RECOVERY_ACTIVE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    context->status.fast_recovery_status = BLE_FAST_RECOVERY_ACTIVE;
    context->status.current_backoff_ms = 0;
    xSemaphoreGive(s_lock);
    context->metrics.current_backoff_ms = 0;
    context->action_due_ms = now;
    transition(side, BLE_CONNECTION_FAST_RECOVERY, reason);
    if (sp624e_controller_is_started()) sp624e_controller_on_fast_recovery(side);
}

static void fail_fast_recovery(sp624e_side_t side, const char *reason)
{
    side_context_t *context = &s_sides[side];
    if (context->metrics.fast_recovery_status == BLE_FAST_RECOVERY_ACTIVE) {
        context->metrics.fast_recovery_failure++;
    }
    context->metrics.fast_recovery_status = BLE_FAST_RECOVERY_FAILED;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    context->status.fast_recovery_status = BLE_FAST_RECOVERY_FAILED;
    xSemaphoreGive(s_lock);
    context->recovery_window.phase = BLE_RECOVERY_PHASE_NORMAL;
    enter_backoff(side, reason, true);
}

static void complete_disconnect(sp624e_side_t side, int reason,
                                const char *description, bool failure,
                                bool unexpected_gap)
{
    side_context_t *context = &s_sides[side];
    int64_t disconnected_at = now_ms();
    int64_t connected_duration = context->metrics.last_connect_timestamp_ms > 0 ?
        disconnected_at - context->metrics.last_connect_timestamp_ms : 0;
    if (context->status.connected && context->metrics.last_connect_timestamp_ms > 0) {
        context->metrics.total_connected_time_ms +=
            (uint64_t)(disconnected_at - context->metrics.last_connect_timestamp_ms);
    }
    if (s_connect_side == side) s_connect_side = -1;
    release_recovery_scan((int)side, "scan cancelled by peer disconnect");
    ble_connection_force_cleanup(side, reason);
    const ble_addr_t *address = ble_connection_get_address(side);
    if (address != NULL) sp624e_controller_on_disconnect(address, reason);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    context->status.connected = false;
    context->status.gatt_ready = false;
    context->status.conn_handle = BLE_CONN_HANDLE_NONE;
    context->recovery_due_ms = 0;
    xSemaphoreGive(s_lock);
    context->metrics.disconnect_count++;
    context->metrics.last_disconnect_reason = reason;
    portENTER_CRITICAL(&s_critical_mux);
    context->metrics.last_ble_rx_ms = s_last_ble_rx_ms[side];
    context->metrics.last_state_query_valid_ms = s_last_valid_state_ms[side];
    portEXIT_CRITICAL(&s_critical_mux);
    context->metrics.last_disconnect_timestamp_ms = disconnected_at;
    context->advertisement_seen = false;
    sp624e_side_t peer = side == SP624E_SIDE_LEFT ? SP624E_SIDE_RIGHT : SP624E_SIDE_LEFT;
    int64_t peer_disconnect = s_sides[peer].metrics.last_disconnect_timestamp_ms;
    bool dual = peer_disconnect > 0 && disconnected_at >= peer_disconnect &&
                disconnected_at - peer_disconnect <= DUAL_DISCONNECT_WINDOW_MS;
    ble_disconnect_evidence_t evidence = {
        .manual = context->manual_disconnect,
        .supervision_timeout = unexpected_gap &&
            reason == BLE_HS_HCI_ERR(BLE_ERR_CONN_SPVN_TMO),
        .connection_establishment_failure = false,
        .stable_link = unexpected_gap && connected_duration >= STABLE_LINK_EVIDENCE_MS,
        .platform_operational = recovery_platform_operational(disconnected_at),
        .peer_dropped_recently = dual,
        .weak_signal = unexpected_gap &&
            context->metrics.last_rssi <= APP_BLE_WEAK_RSSI_DBM,
    };
    ble_disconnect_classification_t classification = ble_recovery_classify(&evidence);
    bool force_fast_test = context->force_fast_test;
    context->manual_disconnect = false;
    context->force_fast_test = false;
    context->metrics.last_disconnect_classification = classification;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    context->status.disconnect_classification = classification;
    xSemaphoreGive(s_lock);
    reset_recovery_trace(context, disconnected_at);
    if (context->metrics.last_ble_rx_ms > 0 &&
        disconnected_at >= context->metrics.last_ble_rx_ms) {
        uint32_t sample = (uint32_t)(disconnected_at - context->metrics.last_ble_rx_ms);
        update_average(sample, &context->detection_samples,
                       &context->metrics.avg_disconnect_detection_ms,
                       &context->metrics.max_disconnect_detection_ms);
    }
    if (evidence.supervision_timeout) context->metrics.supervision_timeout_disconnects++;
    if (dual) {
        context->metrics.dual_disconnect_count++;
        sp624e_group_metrics_t group;
        sp624e_metrics_get_group(&group);
        group.dual_disconnect_count++;
        sp624e_metrics_set_group(&group);
    }
    if (classification == BLE_DISCONNECT_LIKELY_POWER_CYCLE) {
        context->metrics.power_cycle_suspected_count++;
    }
    transition(side, BLE_CONNECTION_DISCONNECTED, description);
    ESP_LOGW(TAG, "%s disconnect classified=%s stable_ms=%" PRId64
                  " dual=%d platform_ok=%d weak=%d",
             sp624e_side_name(side),
             ble_disconnect_classification_name(classification), connected_duration, dual,
             evidence.platform_operational, evidence.weak_signal);
    if (ble_recovery_classification_uses_fast_path(classification) || force_fast_test) {
        enter_fast_recovery(side, force_fast_test ?
            "software power-cycle test; physical cause not inferred" :
            "power-cycle compatible disconnect");
    } else {
        context->metrics.fast_recovery_status = BLE_FAST_RECOVERY_IDLE;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        context->status.fast_recovery_status = BLE_FAST_RECOVERY_IDLE;
        xSemaphoreGive(s_lock);
        enter_backoff(side, failure ? description : "automatic recovery scheduled", failure);
    }
}

static void begin_recovery(sp624e_side_t side, int reason, const char *description)
{
    side_context_t *context = &s_sides[side];
    if (context->status.state == BLE_CONNECTION_RECOVERING) return;
    release_recovery_scan((int)side, "scan cancelled by peer recovery");
    if (s_connect_side == side) s_connect_side = -1;
    context->recovery_reason = reason;
    context->metrics.forced_recoveries++;
    sp624e_metrics_set_side(side, &context->metrics);
    transition(side, BLE_CONNECTION_RECOVERING, description);
    int rc = ble_connection_terminate(side, BLE_ERR_REM_USER_CONN_TERM);
    if (context->status.connected && rc == 0) {
        context->recovery_due_ms = now_ms() + TERMINATION_TIMEOUT_MS;
        return;
    }
    ESP_LOGW(TAG, "%s forced local cleanup terminate_rc=%d",
             sp624e_side_name(side), rc);
    complete_disconnect(side, reason, description, true, false);
}

static void start_connect(sp624e_side_t side, const char *reason)
{
    side_context_t *context = &s_sides[side];
    if (s_connect_side >= 0 || context->status.connected) return;
    bool fast = ble_recovery_window_is_fast(&context->recovery_window, now_ms());
    release_recovery_scan((int)side, "scan cancelled by peer connect");
    context->initial_attempted = true;
    context->advertisement_seen = false;
    context->connect_cancel_pending = false;
    context->metrics.connect_attempts++;
    int64_t connect_started = now_ms();
    bool first_connect_for_recovery = context->metrics.connect_started_ms == 0;
    if (first_connect_for_recovery) {
        context->metrics.connect_started_ms = connect_started;
    }
    if (context->metrics.first_adv_ms > 0 && first_connect_for_recovery &&
        connect_started >= context->metrics.first_adv_ms) {
        uint32_t sample = (uint32_t)(connect_started - context->metrics.first_adv_ms);
        update_average(sample, &context->adv_connect_samples,
                       &context->metrics.avg_adv_to_connect_ms,
                       &context->metrics.max_adv_to_connect_ms);
    }
    sp624e_metrics_set_side(side, &context->metrics);
    transition(side, BLE_CONNECTION_CONNECTING, reason);
    s_connect_side = side;
    context->connect_deadline_ms = now_ms() +
        (fast ? APP_BLE_FAST_CONNECT_TIMEOUT_MS : APP_BLE_CONNECT_TIMEOUT_MS) + 2000;
    int rc = ble_connection_connect(side, fast ? APP_BLE_FAST_CONNECT_TIMEOUT_MS :
                                                 APP_BLE_CONNECT_TIMEOUT_MS);
    if (rc != 0) {
        s_connect_side = -1;
        context->connect_deadline_ms = 0;
        if (fast) {
            context->action_due_ms = now_ms() + APP_BLE_FAST_RETRY_DELAY_MS;
            transition(side, BLE_CONNECTION_FAST_RECOVERY,
                       "connect start failed; return to fast scan");
        } else {
            enter_backoff(side, "ble_gap_connect start failed", true);
        }
    }
}

static void begin_recovery_scan(void)
{
    if (s_scan_mask != 0 || s_connect_side >= 0) return;
    int64_t now = now_ms();
    ble_addr_t addresses[SP624E_SIDE_COUNT];
    uint8_t mask = 0;
    size_t count = 0;
    bool fast = false;
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        side_context_t *context = &s_sides[side];
        if (context->status.connected || now < context->action_due_ms) continue;
        bool side_fast = ble_recovery_window_is_fast(&context->recovery_window, now);
        if (!side_fast && context->status.state != BLE_CONNECTION_BACKOFF) continue;
        const ble_addr_t *address = ble_connection_get_address(side);
        if (address == NULL) {
            enter_backoff(side, "missing mapped address", true);
            continue;
        }
        addresses[count++] = *address;
        mask |= (uint8_t)(1U << side);
        fast = fast || side_fast;
        if (context->metrics.scan_started_ms == 0) {
            context->metrics.scan_started_ms = now;
        }
        transition(side, side_fast ? BLE_CONNECTION_FAST_RECOVERY :
                                     BLE_CONNECTION_WAITING_FOR_ADV,
                   side_fast ? "FAST scan started" : "normal recovery scan started");
    }
    if (count == 0) return;
    int rc = ble_scanner_start_targets(addresses, count, fast);
    if (rc == 0) {
        s_scan_mask = mask;
    } else {
        for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
            if ((mask & (1U << side)) == 0) continue;
            side_context_t *context = &s_sides[side];
            if (ble_recovery_window_is_fast(&context->recovery_window, now)) {
                context->action_due_ms = now + APP_BLE_FAST_RETRY_DELAY_MS;
            } else {
                enter_backoff(side, "target scan start failed", true);
            }
        }
    }
}

static void handle_event(const manager_event_t *event)
{
    sp624e_side_t side = event->side;
    side_context_t *context = &s_sides[side];
    sp624e_metrics_get_side(side, &context->metrics);
    context->metrics.critical_event_replacements = s_critical_replacements[side];
    switch (event->type) {
    case EVENT_START:
        context->metrics.last_rssi = ble_connection_get_rssi(side);
        context->status.rssi = context->metrics.last_rssi;
        if (ble_connection_was_observed(side)) {
            transition(side, BLE_CONNECTION_DISCONNECTED,
                       "mapped peripheral observed during boot scan");
        } else {
            context->initial_attempted = true;
            transition(side, BLE_CONNECTION_BACKOFF,
                       "mapped peripheral absent from boot scan");
        }
        context->action_due_ms = now_ms();
        break;
    case EVENT_CONNECTED:
        s_connect_side = -1;
        context->connect_deadline_ms = 0;
        context->connect_cancel_pending = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        context->status.connected = true;
        context->status.conn_handle = event->conn_handle;
        xSemaphoreGive(s_lock);
        context->metrics.successful_connections++;
        context->metrics.last_connect_timestamp_ms = now_ms();
        context->metrics.connected_ms = context->metrics.last_connect_timestamp_ms;
        if (context->ever_ready) context->metrics.reconnect_success_count++;
        transition(side, BLE_CONNECTION_CONNECTED, "GAP connect success");
        transition(side, BLE_CONNECTION_DISCOVERING, "starting GATT discovery");
        break;
    case EVENT_CONNECT_FAILED:
        s_connect_side = -1;
        context->connect_deadline_ms = 0;
        context->connect_cancel_pending = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        context->status.connected = false;
        xSemaphoreGive(s_lock);
        if (event->reason == BLE_HS_HCI_ERR(BLE_ERR_CONN_ESTABLISHMENT) ||
            event->reason == BLE_ERR_CONN_ESTABLISHMENT) {
            context->metrics.connection_0x3e_count++;
        }
        if (ble_recovery_window_is_fast(&context->recovery_window, now_ms())) {
            context->action_due_ms = now_ms() + APP_BLE_FAST_RETRY_DELAY_MS;
            transition(side, BLE_CONNECTION_FAST_RECOVERY,
                       "0x3E/connect failure; wait next advertisement");
        } else {
            if (context->metrics.fast_recovery_status == BLE_FAST_RECOVERY_ACTIVE) {
                fail_fast_recovery(side, "fast connection window expired");
            } else {
                context->metrics.last_disconnect_classification =
                    BLE_DISCONNECT_CONNECTION_ESTABLISHMENT_FAILURE;
                enter_backoff(side, "connection establishment failed", true);
            }
        }
        break;
    case EVENT_GATT_READY: {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        context->status.gatt_ready = true;
        xSemaphoreGive(s_lock);
        context->metrics.gatt_ready_ms = now_ms();
        transition(side, BLE_CONNECTION_SUBSCRIBING, "FFE0/FFE1/CCCD ready");
        sp624e_transport_t transport;
        if (!ble_connection_get_transport(side, &transport)) {
            begin_recovery(side, BLE_HS_EUNKNOWN, "GATT transport unavailable");
            break;
        }
        context->using_cached_gatt = transport.cached_handles;
        if (transport.cached_handles) context->metrics.gatt_fast_path_attempts++;
        if (!sp624e_controller_is_started()) {
            sp624e_transport_t transports[SP624E_SIDE_COUNT];
            if (ble_connection_get_transport(SP624E_SIDE_LEFT, &transports[0]) &&
                ble_connection_get_transport(SP624E_SIDE_RIGHT, &transports[1]) &&
                s_sides[0].status.gatt_ready && s_sides[1].status.gatt_ready) {
                s_controller_started_ms = now_ms();
                sp624e_controller_start(transports);
            }
        } else {
            sp624e_controller_on_recovered(side, &transport);
        }
        break;
    }
    case EVENT_GATT_FAILED:
        xSemaphoreTake(s_lock, portMAX_DELAY);
        context->status.gatt_ready = false;
        xSemaphoreGive(s_lock);
        begin_recovery(side, event->reason, "GATT discovery failed");
        break;
    case EVENT_DISCONNECTED: {
        if (!context->status.connected &&
            (context->status.state == BLE_CONNECTION_BACKOFF ||
             context->status.state == BLE_CONNECTION_DISCONNECTED)) {
            break;
        }
        bool recovery = context->status.state == BLE_CONNECTION_RECOVERING;
        complete_disconnect(side, event->reason, "GAP disconnect", recovery, !recovery);
        break;
    }
    case EVENT_ADV_FOUND:
        if (context->status.connected ||
            (s_scan_mask & (uint8_t)(1U << side)) == 0) {
            break;
        }
        context->metrics.last_rssi = event->rssi;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        context->status.rssi = event->rssi;
        xSemaphoreGive(s_lock);
        context->advertisement_seen = true;
        context->action_due_ms = now_ms();
        if (context->metrics.first_adv_ms == 0) {
            context->metrics.first_adv_ms = context->action_due_ms;
            if (context->metrics.last_disconnect_timestamp_ms > 0 &&
                context->action_due_ms >= context->metrics.last_disconnect_timestamp_ms) {
                context->metrics.last_adv_after_loss_ms =
                    (uint32_t)(context->action_due_ms -
                               context->metrics.last_disconnect_timestamp_ms);
            }
        }
        release_recovery_scan((int)side, "scan cancelled by peer advertisement");
        break;
    case EVENT_STAGE:
        transition(side, event->state, event->text);
        if (event->state == BLE_CONNECTION_QUERYING_STATE &&
            context->metrics.cccd_subscribed_ms == 0) {
            context->metrics.cccd_subscribed_ms = now_ms();
        }
        if (event->state == BLE_CONNECTION_RECONCILING &&
            context->metrics.reconcile_started_ms == 0) {
            context->metrics.reconcile_started_ms = now_ms();
        }
        if (event->state == BLE_CONNECTION_READY) {
            context->ever_ready = true;
            context->metrics.current_backoff_ms = 0;
            if (context->metrics.first_adv_ms > 0 &&
                !context->recovery_ready_recorded) {
                uint32_t sample = (uint32_t)(now_ms() - context->metrics.first_adv_ms);
                update_average(sample, &context->adv_ready_samples,
                               &context->metrics.avg_adv_to_ready_ms,
                               &context->metrics.max_adv_to_ready_ms);
                context->recovery_ready_recorded = true;
            }
            if (context->metrics.fast_recovery_status == BLE_FAST_RECOVERY_ACTIVE) {
                context->metrics.fast_recovery_success++;
                context->metrics.fast_recovery_status = BLE_FAST_RECOVERY_PASS;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                context->status.fast_recovery_status = BLE_FAST_RECOVERY_PASS;
                xSemaphoreGive(s_lock);
                context->recovery_window.phase = BLE_RECOVERY_PHASE_NORMAL;
            }
            if (context->using_cached_gatt) {
                context->metrics.gatt_fast_path_success++;
                context->using_cached_gatt = false;
            }
        }
        break;
    case EVENT_UNHEALTHY:
        begin_recovery(side, event->reason, event->text);
        break;
    case EVENT_MANUAL_DISCONNECT:
        if (context->status.connected) {
            context->manual_disconnect = true;
            begin_recovery(side, BLE_ERR_REM_USER_CONN_TERM,
                           "manual disconnect requested");
        }
        break;
    case EVENT_MANUAL_RECONNECT:
        if (!context->status.connected) {
            context->action_due_ms = now_ms();
            transition(side, BLE_CONNECTION_BACKOFF, "manual reconnect requested");
        }
        break;
    case EVENT_POWER_CYCLE_TEST:
        if (context->status.connected) {
            context->manual_disconnect = true;
            context->force_fast_test = true;
            begin_recovery(side, BLE_ERR_REM_USER_CONN_TERM,
                           "software power-cycle recovery test");
        }
        break;
    case EVENT_BLE_RX:
        context->metrics.last_ble_rx_ms = event->timestamp_ms;
        break;
    case EVENT_VALID_STATE:
        context->metrics.last_state_query_valid_ms = event->timestamp_ms;
        if (context->metrics.state_valid_ms == 0 &&
            context->metrics.recovery_started_ms > 0) {
            context->metrics.state_valid_ms = event->timestamp_ms;
        }
        break;
    case EVENT_GROUP_SYNCED:
        if (context->metrics.last_disconnect_timestamp_ms > 0 &&
            context->metrics.group_synced_ms == 0) {
            context->metrics.group_synced_ms = now_ms();
            uint32_t sample = (uint32_t)(context->metrics.group_synced_ms -
                                         context->metrics.last_disconnect_timestamp_ms);
            context->metrics.last_recovery_duration_ms = sample;
            update_average(sample, &context->disconnect_synced_samples,
                           &context->metrics.avg_disconnect_to_synced_ms,
                           &context->metrics.max_disconnect_to_synced_ms);
            log_power_cycle_trace(side, context);
        }
        break;
    case EVENT_CONNECTION_PARAMS:
        context->metrics.requested_supervision_timeout_ms = event->requested_ms;
        context->metrics.accepted_supervision_timeout_ms = event->accepted_ms;
        break;
    case EVENT_FAST_GATT_FAILED:
        context->metrics.gatt_fast_path_fallbacks++;
        context->using_cached_gatt = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        context->status.gatt_ready = false;
        xSemaphoreGive(s_lock);
        transition(side, BLE_CONNECTION_DISCOVERING,
                   "cached GATT handles failed; full discovery");
        if (ble_connection_start_full_discovery(side) != 0) {
            begin_recovery(side, event->reason, "full GATT fallback failed to start");
        }
        break;
    default:
        break;
    }
    sp624e_metrics_set_side(side, &context->metrics);
}

static bool both_connected(void)
{
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        if (!s_sides[side].status.connected) return false;
    }
    return true;
}

static void process_timers(void)
{
    int64_t now = now_ms();
    if (s_scan_mask != 0 && !ble_scanner_is_active()) {
        uint8_t expired = s_scan_mask;
        s_scan_mask = 0;
        for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
            if ((expired & (1U << side)) == 0) continue;
            side_context_t *context = &s_sides[side];
            if (context->metrics.fast_recovery_status == BLE_FAST_RECOVERY_ACTIVE) {
                fail_fast_recovery(side, "FAST scan window expired; normal fallback");
            } else {
                enter_absent_backoff(side, "mapped peripheral absent");
            }
        }
    }
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        side_context_t *context = &s_sides[side];
        if (context->status.state == BLE_CONNECTION_READY &&
            context->status.reconnect_attempt > 0 && context->status.ready_since_ms > 0 &&
            now - context->status.ready_since_ms >= READY_BACKOFF_RESET_MS) {
            context->status.reconnect_attempt = 0;
            context->status.current_backoff_ms = 0;
            context->metrics.current_backoff_ms = 0;
            ESP_LOGI(TAG, "%s READY stable 10s; reconnect backoff reset",
                     sp624e_side_name(side));
        }
        if (context->status.state == BLE_CONNECTION_DISCOVERING &&
            now - context->status.state_entered_ms > DISCOVERY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "%s GATT discovery timeout", sp624e_side_name(side));
            begin_recovery(side, BLE_HS_ETIMEOUT, "GATT discovery timeout");
        }
        int64_t pipeline_started_ms = context->status.state_entered_ms;
        if (context->status.state == BLE_CONNECTION_SUBSCRIBING &&
            s_controller_started_ms > pipeline_started_ms) {
            pipeline_started_ms = s_controller_started_ms;
        }
        if (((context->status.state == BLE_CONNECTION_SUBSCRIBING &&
              sp624e_controller_is_started()) ||
             context->status.state == BLE_CONNECTION_QUERYING_STATE ||
             context->status.state == BLE_CONNECTION_RECONCILING) &&
            now - pipeline_started_ms > PIPELINE_TIMEOUT_MS) {
            ESP_LOGE(TAG, "%s recovery pipeline timeout state=%s", sp624e_side_name(side),
                     ble_connection_state_name(context->status.state));
            begin_recovery(side, BLE_HS_ETIMEOUT, "recovery pipeline timeout");
        }
        if (context->status.state == BLE_CONNECTION_CONNECTING &&
            context->connect_deadline_ms > 0 && now >= context->connect_deadline_ms) {
            if (!context->connect_cancel_pending) {
                context->connect_cancel_pending = true;
                context->connect_deadline_ms = now + TERMINATION_TIMEOUT_MS;
                int rc = ble_connection_cancel_connect(side);
                ESP_LOGE(TAG, "%s connection callback timeout; cancel rc=%d",
                         sp624e_side_name(side), rc);
            } else {
                ESP_LOGE(TAG, "%s connection cancel callback timeout; local reset",
                         sp624e_side_name(side));
                context->connect_cancel_pending = false;
                context->connect_deadline_ms = 0;
                s_connect_side = -1;
                ble_connection_force_cleanup(side, BLE_HS_ETIMEOUT);
                context->metrics.last_disconnect_classification =
                    BLE_DISCONNECT_CONNECTION_ESTABLISHMENT_FAILURE;
                if (ble_recovery_window_is_fast(&context->recovery_window, now)) {
                    context->action_due_ms = now + APP_BLE_FAST_RETRY_DELAY_MS;
                    transition(side, BLE_CONNECTION_FAST_RECOVERY,
                               "connect cancel timeout; return to fast scan");
                } else {
                    enter_backoff(side, "connect cancel timeout", true);
                }
            }
        }
        if (context->status.state == BLE_CONNECTION_FAST_RECOVERY &&
            context->metrics.fast_recovery_status == BLE_FAST_RECOVERY_ACTIVE &&
            !ble_recovery_window_is_fast(&context->recovery_window, now)) {
            if (s_scan_mask != 0) {
                ble_scanner_stop_scan();
                s_scan_mask = 0;
            }
            fail_fast_recovery(side, "FAST recovery window expired; normal fallback");
        }
        if (context->status.state == BLE_CONNECTION_RECOVERING &&
            context->recovery_due_ms > 0 && now >= context->recovery_due_ms) {
            ESP_LOGE(TAG, "%s disconnect callback timeout", sp624e_side_name(side));
            complete_disconnect(side, context->recovery_reason,
                                "disconnect callback timeout", true, false);
        }
    }

    if (s_connect_side >= 0 || s_scan_mask != 0) return;
    if (s_master_guard_count > 0 && !both_connected()) {
        /* Runs on this task immediately before start_connect/begin_recovery_scan,
           so the master role is free by the time either of them asks for it. */
        for (size_t i = 0; i < s_master_guard_count; ++i) {
            s_master_guards[i]("SP624E recovery requires the BLE master role");
        }
    }
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        side_context_t *context = &s_sides[side];
        if (context->status.connected) continue;
        if (context->status.state == BLE_CONNECTION_DISCONNECTED &&
            !context->initial_attempted && now >= context->action_due_ms) {
            start_connect(side, "initial advertisement already observed");
            return;
        }
        if ((context->status.state == BLE_CONNECTION_WAITING_FOR_ADV ||
             context->status.state == BLE_CONNECTION_FAST_RECOVERY) &&
            context->advertisement_seen && now >= context->action_due_ms) {
            start_connect(side, "target advertisement found");
            return;
        }
    }
    begin_recovery_scan();
}

static void manager_task(void *arg)
{
    (void)arg;
    bool watched = esp_task_wdt_add(NULL) == ESP_OK;
    while (true) {
        manager_event_t event;
        if (take_critical_event(&event) ||
            xQueueReceive(s_event_queue, &event, pdMS_TO_TICKS(MANAGER_TICK_MS)) == pdTRUE) {
            handle_event(&event);
        }
        process_timers();
        portENTER_CRITICAL(&s_critical_mux);
        s_heartbeat_ms = now_ms();
        portEXIT_CRITICAL(&s_critical_mux);
        if (watched) esp_task_wdt_reset();
    }
}

esp_err_t ble_connection_manager_add_master_guard(ble_master_guard_fn guard)
{
    if (guard == NULL) return ESP_ERR_INVALID_ARG;
    if (s_master_guard_count >= BLE_MASTER_GUARD_MAX) return ESP_ERR_NO_MEM;
    s_master_guards[s_master_guard_count++] = guard;
    return ESP_OK;
}

void ble_connection_manager_start(void)
{
    if (s_started) return;
    memset(s_sides, 0, sizeof(s_sides));
    memset(s_critical_pending, 0, sizeof(s_critical_pending));
    memset(s_critical_replacements, 0, sizeof(s_critical_replacements));
    memset(s_last_ble_rx_ms, 0, sizeof(s_last_ble_rx_ms));
    memset(s_last_valid_state_ms, 0, sizeof(s_last_valid_state_ms));
    s_controller_started_ms = 0;
    s_scan_mask = 0;
    s_connect_side = -1;
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    (void)s_lock;
    s_event_queue = xQueueCreateStatic(MANAGER_EVENT_DEPTH, sizeof(manager_event_t),
                                       s_event_queue_buffer, &s_event_queue_storage);
    sp624e_metrics_init();
    s_started = true;
    xTaskCreate(manager_task, "connection_mgr", 6144, NULL, 7, NULL);
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        manager_event_t event = {.type = EVENT_START, .side = side};
        post_event(&event);
    }
}

int64_t ble_connection_manager_heartbeat_ms(void)
{
    portENTER_CRITICAL(&s_critical_mux);
    int64_t heartbeat = s_heartbeat_ms;
    portEXIT_CRITICAL(&s_critical_mux);
    return heartbeat;
}

void ble_connection_manager_on_connected(sp624e_side_t side, uint16_t conn_handle)
{
    manager_event_t event = {.type = EVENT_CONNECTED, .side = side,
                             .conn_handle = conn_handle};
    post_event(&event);
}

void ble_connection_manager_on_connect_failed(sp624e_side_t side, int reason)
{
    manager_event_t event = {.type = EVENT_CONNECT_FAILED, .side = side, .reason = reason};
    post_event(&event);
}

void ble_connection_manager_on_gatt_ready(sp624e_side_t side)
{
    manager_event_t event = {.type = EVENT_GATT_READY, .side = side};
    post_event(&event);
}

void ble_connection_manager_on_gatt_failed(sp624e_side_t side, int reason)
{
    manager_event_t event = {.type = EVENT_GATT_FAILED, .side = side, .reason = reason};
    post_event(&event);
}

void ble_connection_manager_on_disconnected(sp624e_side_t side, int reason)
{
    manager_event_t event = {.type = EVENT_DISCONNECTED, .side = side, .reason = reason};
    post_event(&event);
}

void ble_connection_manager_on_advertisement(const ble_addr_t *address, int8_t rssi)
{
    if (address == NULL) return;
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        const ble_addr_t *candidate = ble_connection_get_address(side);
        if (candidate != NULL && candidate->type == address->type &&
            memcmp(candidate->val, address->val, sizeof(address->val)) == 0) {
            manager_event_t event = {.type = EVENT_ADV_FOUND, .side = side, .rssi = rssi};
            post_event(&event);
            return;
        }
    }
}

void ble_connection_manager_on_valid_state(sp624e_side_t side)
{
    if (side >= SP624E_SIDE_COUNT) return;
    int64_t timestamp_ms = now_ms();
    portENTER_CRITICAL(&s_critical_mux);
    s_last_valid_state_ms[side] = timestamp_ms;
    portEXIT_CRITICAL(&s_critical_mux);
    manager_event_t event = {
        .type = EVENT_VALID_STATE,
        .side = side,
        .timestamp_ms = timestamp_ms,
    };
    post_event(&event);
}

void ble_connection_manager_on_ble_rx(sp624e_side_t side, int64_t received_ms)
{
    if (side >= SP624E_SIDE_COUNT || received_ms <= 0) return;
    portENTER_CRITICAL(&s_critical_mux);
    s_last_ble_rx_ms[side] = received_ms;
    portEXIT_CRITICAL(&s_critical_mux);
    manager_event_t event = {
        .type = EVENT_BLE_RX,
        .side = side,
        .timestamp_ms = received_ms,
    };
    post_event(&event);
}

void ble_connection_manager_on_group_synced(void)
{
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        manager_event_t event = {.type = EVENT_GROUP_SYNCED, .side = side};
        post_event(&event);
    }
}

void ble_connection_manager_on_connection_params(sp624e_side_t side,
                                                 uint16_t requested_ms,
                                                 uint16_t accepted_ms)
{
    manager_event_t event = {
        .type = EVENT_CONNECTION_PARAMS,
        .side = side,
        .requested_ms = requested_ms,
        .accepted_ms = accepted_ms,
    };
    post_event(&event);
}

void ble_connection_manager_on_fast_gatt_failed(sp624e_side_t side, int reason)
{
    manager_event_t event = {
        .type = EVENT_FAST_GATT_FAILED,
        .side = side,
        .reason = reason,
    };
    post_event(&event);
}

void ble_connection_manager_set_stage(sp624e_side_t side,
                                      ble_connection_state_t state,
                                      const char *reason)
{
    manager_event_t event = {.type = EVENT_STAGE, .side = side, .state = state};
    if (reason != NULL) snprintf(event.text, sizeof(event.text), "%s", reason);
    post_event(&event);
}

void ble_connection_manager_mark_unhealthy(sp624e_side_t side, const char *reason)
{
    manager_event_t event = {.type = EVENT_UNHEALTHY, .side = side};
    if (reason != NULL) snprintf(event.text, sizeof(event.text), "%s", reason);
    post_event(&event);
}

void ble_connection_manager_request_disconnect(sp624e_side_t side)
{
    manager_event_t event = {.type = EVENT_MANUAL_DISCONNECT, .side = side};
    post_event(&event);
}

void ble_connection_manager_request_power_cycle_test(sp624e_side_t side)
{
    manager_event_t event = {.type = EVENT_POWER_CYCLE_TEST, .side = side};
    post_event(&event);
}

void ble_connection_manager_request_reconnect(sp624e_side_t side)
{
    manager_event_t event = {.type = EVENT_MANUAL_RECONNECT, .side = side};
    post_event(&event);
}

bool ble_connection_manager_is_ready(sp624e_side_t side)
{
    if (side >= SP624E_SIDE_COUNT || s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ready = s_sides[side].status.state == BLE_CONNECTION_READY;
    xSemaphoreGive(s_lock);
    return ready;
}

bool ble_connection_manager_both_ready(void)
{
    return ble_connection_manager_is_ready(SP624E_SIDE_LEFT) &&
           ble_connection_manager_is_ready(SP624E_SIDE_RIGHT);
}

bool ble_connection_manager_is_sync_eligible(sp624e_side_t side)
{
    if (side >= SP624E_SIDE_COUNT || s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ble_connection_state_t state = s_sides[side].status.state;
    bool eligible = s_sides[side].status.connected &&
        (state == BLE_CONNECTION_SYNC_PENDING ||
         state == BLE_CONNECTION_RECONCILING ||
         state == BLE_CONNECTION_READY);
    xSemaphoreGive(s_lock);
    return eligible;
}

bool ble_connection_manager_both_sync_eligible(void)
{
    return ble_connection_manager_is_sync_eligible(SP624E_SIDE_LEFT) &&
           ble_connection_manager_is_sync_eligible(SP624E_SIDE_RIGHT);
}

bool ble_connection_manager_any_fast_recovery(void)
{
    if (s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool active = false;
    for (sp624e_side_t side = SP624E_SIDE_LEFT; side < SP624E_SIDE_COUNT; ++side) {
        if (s_sides[side].status.fast_recovery_status == BLE_FAST_RECOVERY_ACTIVE) {
            active = true;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return active;
}

void ble_connection_manager_get_status(sp624e_side_t side,
                                       ble_connection_manager_status_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (side >= SP624E_SIDE_COUNT || s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_sides[side].status;
    xSemaphoreGive(s_lock);
}
