#include "sp624e_controller.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "ble/ble_connection_manager.h"
#include "diagnostics/connection_metrics.h"
#include "console/runtime_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "sdkconfig.h"
#include "sp624e_command_queue.h"
#include "sp624e_protocol.h"
#include "sp624e_provisioning.h"
#include "sp624e_state.h"
#include "presets/preset_manager.h"
#include "sync/desired_state.h"
#include "sync/state_reconciler.h"
#include "animation/animation_manager.h"
#include "interior/interior_light.h"

#define WRITE_TIMEOUT_MS 2000
#define STATE_TIMEOUT_MS 2500
#define RECONCILE_TIMEOUT_MS 5000
#define DEPENDENT_DELAY_MS 60
#define EXTERNAL_COOLDOWN_MS 2000
#define PERSIST_DEBOUNCE_MS 2000
#define CONSOLE_LINE_MAX 128
#define CONSOLE_QUEUE_DEPTH 8
#define NOTIFY_QUEUE_DEPTH 16
#define TEST_QUEUE_DEPTH 4
#define GROUP_API_QUEUE_DEPTH 16
#define GROUP_API_RESPONSE_DEPTH 16
#define GROUP_API_LOCK_TIMEOUT_MS 250
#define GROUP_API_RESPONSE_TIMEOUT_MS 1000
#define ANIMATION_CANCEL_WAIT_GRACE_MS 500

static const char *TAG = "GROUP_CTRL";
static const char *TAG_CMD = "COMMAND_QUEUE";
static const char *TAG_STATE = "STATE_RECONCILER";
static const char *TAG_NOTIFY = "SP624E_NOTIFY";
static const char *TAG_TEST = "RELIABILITY_TEST";

typedef struct {
    uint16_t conn_handle;
    uint16_t attr_handle;
    uint8_t length;
    uint8_t data[SP624E_MESSAGE_MAX_LEN + 3];
} notify_packet_t;

typedef enum {
    TEST_REQUEST_SYNC = 0,
    TEST_REQUEST_RECONNECT,
    TEST_REQUEST_STRESS,
    TEST_REQUEST_MIDFAIL,
    TEST_REQUEST_STABILITY,
    TEST_REQUEST_RESTORE,
    TEST_REQUEST_WHITE,
} test_request_type_t;

typedef struct {
    test_request_type_t type;
    sp624e_side_t side;
    uint32_t duration_seconds;
} test_request_t;

typedef enum {
    GROUP_API_SET_RGB = 0,
    GROUP_API_SET_WHITE,
    GROUP_API_RESYNC,
} group_api_request_type_t;

typedef struct {
    uint32_t id;
    group_api_request_type_t type;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t level;
} group_api_request_t;

typedef struct {
    uint32_t id;
    sp624e_group_api_result_t result;
    uint32_t generation;
} group_api_response_t;

typedef struct {
    sp624e_side_t side;
    sp624e_transport_t transport;
    sp624e_reassembly_t reassembly;
    sp624e_light_state_t observed;
    sp624e_light_state_t original;
    bool original_valid;
    bool connected;
    bool notifications_enabled;
    bool initial_query_done;
    bool reconcile_queued;
    bool force_desired_write;
    bool write_busy;
    int write_status;
    uint32_t state_sequence;
    uint32_t last_applied_generation;
    uint32_t last_verified_generation;
    uint32_t generation_at_disconnect;
    int64_t health_due_ms;
    int64_t external_cooldown_until_ms;
    sp624e_command_queue_t queue;
    TaskHandle_t worker_task;
    StaticSemaphore_t write_done_storage;
    SemaphoreHandle_t write_done;
    StaticSemaphore_t state_done_storage;
    SemaphoreHandle_t state_done;
} side_context_t;

static side_context_t s_sides[SP624E_SIDE_COUNT];
static sp624e_desired_state_t s_desired;
static sp624e_group_state_t s_group_state = SP624E_GROUP_UNINITIALIZED;
static sp624e_group_metrics_t s_group_metrics;
static bool s_restore_on_boot;
static bool s_desired_was_persisted;
static bool s_authority;
static bool s_temporary_desired;
static bool s_boot_evaluated;
static bool s_hold_strict_dispatch;
static bool s_started;
static int64_t s_persist_due_ms;
static uint32_t s_persisted_generation;
static int64_t s_last_status_ms;

static void maybe_schedule_group_reconcile(void);
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static StaticQueue_t s_notify_queue_storage;
static uint8_t s_notify_queue_buffer[NOTIFY_QUEUE_DEPTH * sizeof(notify_packet_t)];
static QueueHandle_t s_notify_queue;
static StaticQueue_t s_console_queue_storage;
static uint8_t s_console_queue_buffer[CONSOLE_QUEUE_DEPTH * CONSOLE_LINE_MAX];
static QueueHandle_t s_console_queue;
static StaticQueue_t s_test_queue_storage;
static uint8_t s_test_queue_buffer[TEST_QUEUE_DEPTH * sizeof(test_request_t)];
static QueueHandle_t s_test_queue;
static StaticQueue_t s_group_api_queue_storage;
static uint8_t s_group_api_queue_buffer[GROUP_API_QUEUE_DEPTH * sizeof(group_api_request_t)];
static QueueHandle_t s_group_api_queue;
static StaticSemaphore_t s_group_api_call_lock_storage;
static SemaphoreHandle_t s_group_api_call_lock;
static StaticQueue_t s_group_api_response_queue_storage;
static uint8_t s_group_api_response_queue_buffer[
    GROUP_API_RESPONSE_DEPTH * sizeof(group_api_response_t)];
static QueueHandle_t s_group_api_response_queue;
static uint32_t s_group_api_next_id;
static int64_t s_runtime_heartbeat_ms;
static uint32_t s_group_api_timeouts;
static uint32_t s_group_api_busy;
static uint32_t s_group_api_response_drops;
static portMUX_TYPE s_runtime_health_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_white_available;
static bool s_white_test_pending_confirmation;
static bool s_white_test_running;
static bool s_animation_active;
static uint32_t s_animation_generation;
static int s_animation_mode = -1;
static uint32_t s_animation_frames_sent[SP624E_SIDE_COUNT];
static uint32_t s_animation_coalesced_base[SP624E_SIDE_COUNT];
static uint32_t s_animation_max_queue_depth;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static void runtime_health_increment(uint32_t *counter)
{
    portENTER_CRITICAL(&s_runtime_health_mux);
    (*counter)++;
    portEXIT_CRITICAL(&s_runtime_health_mux);
}

static side_context_t *side_by_address(const ble_addr_t *address)
{
    if (address == NULL) return NULL;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        ble_device_entry_t *entry = s_sides[i].transport.entry;
        if (entry != NULL && entry->address.type == address->type &&
            memcmp(entry->address.val, address->val, sizeof(address->val)) == 0) {
            return &s_sides[i];
        }
    }
    return NULL;
}

static side_context_t *side_by_handle(uint16_t conn_handle)
{
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        if (s_sides[i].connected && s_sides[i].transport.conn_handle == conn_handle) {
            return &s_sides[i];
        }
    }
    return NULL;
}

static bool transport_safe(const side_context_t *side)
{
    if (side == NULL || !side->connected || side->transport.entry == NULL ||
        !side->transport.signature_confirmed || !side->transport.ffe0_found ||
        !side->transport.ffe1_found || side->transport.ffe1_handle == 0) return false;
    ble_device_status_t status;
    ble_registry_get_status(side->transport.entry, &status);
    return status.connected && status.conn_handle == side->transport.conn_handle;
}

static bool side_is_connected(const side_context_t *side)
{
    lock();
    bool connected = side != NULL && side->connected;
    unlock();
    return connected;
}

static void update_side_metrics(sp624e_side_t side,
                                void (*update)(sp624e_connection_metrics_t *))
{
    sp624e_connection_metrics_t metrics;
    sp624e_metrics_get_side(side, &metrics);
    update(&metrics);
    metrics.queue_depth = (uint32_t)sp624e_command_queue_depth(&s_sides[side].queue);
    if (metrics.queue_depth > metrics.max_queue_depth) metrics.max_queue_depth = metrics.queue_depth;
    metrics.coalesced_commands = sp624e_command_queue_coalesced_count(&s_sides[side].queue);
    sp624e_metrics_set_side(side, &metrics);
}

static void metric_command(sp624e_connection_metrics_t *m) { m->command_count++; }
static void metric_command_failure(sp624e_connection_metrics_t *m) { m->command_failures++; }
static void metric_query(sp624e_connection_metrics_t *m) { m->state_query_count++; }
static void metric_query_failure(sp624e_connection_metrics_t *m) { m->state_query_failures++; }
static void metric_reconcile(sp624e_connection_metrics_t *m) { m->reconcile_count++; }
static void metric_reconcile_failure(sp624e_connection_metrics_t *m) { m->reconcile_failures++; }

static bool group_can_be_synced(void)
{
    bool both_ready = ble_connection_manager_both_ready();
    lock();
    bool synced = s_desired.valid && s_sides[0].connected && s_sides[1].connected &&
                  sp624e_group_generation_is_synced(
                      s_desired.generation,
                      s_sides[0].last_verified_generation,
                      s_sides[1].last_verified_generation,
                      both_ready);
    unlock();
    return synced;
}

static void set_group_state(sp624e_group_state_t state, const char *reason)
{
    if (state == SP624E_GROUP_SYNCED && !group_can_be_synced()) return;
    int64_t now = now_ms();
    lock();
    if (s_group_state == state) {
        unlock();
        return;
    }
    sp624e_group_state_t previous = s_group_state;
    s_group_state = state;
    if (state == SP624E_GROUP_SYNCED) {
        s_group_metrics.group_sync_count++;
        s_group_metrics.last_sync_time_ms = now;
        if (s_group_metrics.desync_started_ms > 0) {
            uint64_t duration = (uint64_t)(now - s_group_metrics.desync_started_ms);
            if (duration > s_group_metrics.max_desync_duration_ms) {
                s_group_metrics.max_desync_duration_ms = duration;
            }
            ESP_LOGI(TAG, "GROUP desync duration=%" PRIu64 "ms", duration);
            s_group_metrics.desync_started_ms = 0;
        }
    } else if (state == SP624E_GROUP_DEGRADED ||
               state == SP624E_GROUP_POWER_CYCLE_RECOVERY) {
        s_group_metrics.group_degraded_count++;
        if (s_group_metrics.desync_started_ms == 0) s_group_metrics.desync_started_ms = now;
    } else if (state == SP624E_GROUP_UNSYNCED) {
        s_group_metrics.group_desync_count++;
        if (s_group_metrics.desync_started_ms == 0) s_group_metrics.desync_started_ms = now;
    }
    unlock();
    ESP_LOGI(TAG, "GROUP %s -> %s reason=%s", sp624e_group_state_name(previous),
             sp624e_group_state_name(state), reason != NULL ? reason : "unspecified");
    lock();
    sp624e_group_metrics_t current_metrics = s_group_metrics;
    unlock();
    sp624e_metrics_set_group(&current_metrics);
    if (state == SP624E_GROUP_SYNCED) {
        ble_connection_manager_on_group_synced();
    }
}

static int write_callback(uint16_t conn_handle, const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    side_context_t *side = arg;
    if (side == NULL || conn_handle != side->transport.conn_handle ||
        !transport_safe(side)) {
        ESP_LOGW(TAG_CMD, "Ignoring stale GATT callback conn=%u", conn_handle);
        return 0;
    }
    side->write_status = error->status;
    side->write_busy = false;
    xSemaphoreGive(side->write_done);
    return 0;
}

static bool write_once(side_context_t *side, const char *name, uint16_t handle,
                       const uint8_t *payload, size_t length)
{
    if (!transport_safe(side) || side->write_busy) return false;
    while (xSemaphoreTake(side->write_done, 0) == pdTRUE) {}
    side->write_busy = true;
    side->write_status = BLE_HS_EUNKNOWN;
    int rc = ble_gattc_write_flat(side->transport.conn_handle, handle, payload, length,
                                  write_callback, side);
    if (rc != 0) {
        side->write_busy = false;
        side->write_status = rc;
        return false;
    }
    if (xSemaphoreTake(side->write_done, pdMS_TO_TICKS(WRITE_TIMEOUT_MS)) != pdTRUE) {
        side->write_busy = false;
        ESP_LOGE(TAG_CMD, "%s write timeout command=%s", sp624e_side_name(side->side), name);
        return false;
    }
    return side->write_status == 0;
}

static bool execute_write(side_context_t *side, const char *name, uint16_t handle,
                          const uint8_t *payload, size_t length)
{
    update_side_metrics(side->side, metric_command);
    for (int attempt = 0; attempt <= 1; ++attempt) {
        ESP_LOGI(TAG_CMD, "%s TX command=%s attempt=%d/2 len=%u",
                 sp624e_side_name(side->side), name, attempt + 1, (unsigned)length);
        if (write_once(side, name, handle, payload, length)) return true;
        if (!transport_safe(side)) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!side_is_connected(side)) {
        ESP_LOGI(TAG_CMD, "%s command=%s canceled by disconnect",
                 sp624e_side_name(side->side), name);
        return false;
    }
    update_side_metrics(side->side, metric_command_failure);
    ESP_LOGE(TAG_CMD, "%s command=%s failed after bounded retry",
             sp624e_side_name(side->side), name);
    if (!(side->transport.cached_handles && !side->initial_query_done)) {
        ble_connection_manager_mark_unhealthy(side->side, "GATT write failed twice");
    }
    return false;
}

static bool enable_notifications(side_context_t *side)
{
    if (side->transport.cccd_handle == 0 || !side->transport.cccd_found) return false;
    const uint8_t enable[] = {0x01, 0x00};
    if (!execute_write(side, "ENABLE_NOTIFY", side->transport.cccd_handle,
                       enable, sizeof(enable))) return false;
    side->notifications_enabled = true;
    return true;
}

static bool query_state(side_context_t *side)
{
    update_side_metrics(side->side, metric_query);
    while (xSemaphoreTake(side->state_done, 0) == pdTRUE) {}
    if (!side_is_connected(side)) return false;
    uint32_t prior = side->state_sequence;
    uint8_t payload[SP624E_COMMAND_MAX_LEN];
    size_t length = 0;
    if (sp624e_build_state_query(payload, sizeof(payload), &length) != SP624E_PROTOCOL_OK ||
        !execute_write(side, "STATE_QUERY", side->transport.ffe1_handle, payload, length)) {
        update_side_metrics(side->side, metric_query_failure);
        return false;
    }
    int64_t deadline = now_ms() + STATE_TIMEOUT_MS;
    while (now_ms() < deadline) {
        if (!side_is_connected(side)) return false;
        TickType_t remaining = pdMS_TO_TICKS((uint32_t)(deadline - now_ms()));
        if (xSemaphoreTake(side->state_done, remaining) != pdTRUE) break;
        if (!side_is_connected(side)) return false;
        if (side->state_sequence > prior && side->observed.valid) return true;
    }
    update_side_metrics(side->side, metric_query_failure);
    ESP_LOGE(TAG_STATE, "%s state query timeout", sp624e_side_name(side->side));
    return false;
}

static bool send_reconcile_command(side_context_t *side,
                                   const sp624e_reconcile_command_t *command)
{
    uint8_t payload[SP624E_COMMAND_MAX_LEN];
    size_t length = 0;
    const char *name = "UNKNOWN";
    sp624e_protocol_result_t result = SP624E_PROTOCOL_INVALID_ARGUMENT;
    if (command->type == SP624E_RECONCILE_CMD_EFFECT_SOLID) {
        name = "SOLID";
        result = sp624e_build_effect(SP624E_EFFECT_SOLID, payload, sizeof(payload), &length);
    } else if (command->type == SP624E_RECONCILE_CMD_EFFECT_WHITE) {
        name = "WHITE_EFFECT";
        result = sp624e_build_effect(SP624E_EFFECT_WHITE, payload, sizeof(payload), &length);
    } else if (command->type == SP624E_RECONCILE_CMD_RGB) {
        name = "RGB";
        result = sp624e_build_rgb(command->red, command->green, command->blue,
                                  command->level, payload, sizeof(payload), &length);
    } else if (command->type == SP624E_RECONCILE_CMD_BRIGHTNESS) {
        name = "BRIGHTNESS";
        result = sp624e_build_brightness(command->level, payload, sizeof(payload), &length);
    } else if (command->type == SP624E_RECONCILE_CMD_WHITE) {
        name = "WHITE_LEVEL";
        result = sp624e_build_white(command->level, payload, sizeof(payload), &length);
    }
    return result == SP624E_PROTOCOL_OK &&
           execute_write(side, name, side->transport.ffe1_handle, payload, length);
}

static bool reconcile(side_context_t *side, uint32_t generation)
{
    update_side_metrics(side->side, metric_reconcile);
    sp624e_desired_state_t desired;
    lock(); desired = s_desired; unlock();
    if (!desired.valid || desired.generation != generation) return true;
    lock(); bool force_write = side->force_desired_write; unlock();
    sp624e_reconcile_plan_t plan = force_write ?
        sp624e_reconciler_force_plan(&desired) :
        sp624e_reconciler_plan(&desired, &side->observed);
    if (!plan.valid || plan.unsupported_difference) {
        ESP_LOGE(TAG_STATE, "%s RECONCILE unsupported/invalid generation=%" PRIu32,
                 sp624e_side_name(side->side), generation);
        update_side_metrics(side->side, metric_reconcile_failure);
        return false;
    }
    if (plan.already_synchronized) {
        ESP_LOGI(TAG_STATE, "%s RECONCILE: already synchronized generation=%" PRIu32,
                 sp624e_side_name(side->side), generation);
    }
    int64_t deadline = now_ms() + RECONCILE_TIMEOUT_MS;
    for (size_t i = 0; i < plan.count; ++i) {
        if (now_ms() >= deadline || !send_reconcile_command(side, &plan.commands[i])) {
            update_side_metrics(side->side, metric_reconcile_failure);
            return false;
        }
        if (i + 1 < plan.count) vTaskDelay(pdMS_TO_TICKS(DEPENDENT_DELAY_MS));
    }
    side->last_applied_generation = generation;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (!query_state(side) || !sp624e_reconciler_matches(&desired, &side->observed)) {
        ESP_LOGE(TAG_STATE, "%s verification mismatch generation=%" PRIu32,
                 sp624e_side_name(side->side), generation);
        update_side_metrics(side->side, metric_reconcile_failure);
        return false;
    }
    lock();
    side->last_verified_generation = generation;
    side->force_desired_write = false;
    s_group_metrics.verified_generation[side->side] = generation;
    sp624e_group_metrics_t metrics = s_group_metrics;
    unlock();
    sp624e_metrics_set_group(&metrics);
    ESP_LOGI(TAG_STATE, "%s VERIFIED generation=%" PRIu32,
             sp624e_side_name(side->side), generation);
    return true;
}

static bool restore_original(side_context_t *side)
{
    if (!side->original_valid) return false;
    const sp624e_light_state_t original = side->original;
    uint8_t payload[SP624E_COMMAND_MAX_LEN];
    size_t length = 0;
    bool ok;
    if (original.effect == SP624E_EFFECT_WHITE) {
        ok = sp624e_build_effect(SP624E_EFFECT_WHITE, payload, sizeof(payload), &length) == 0 &&
             execute_write(side, "RESTORE_WHITE_EFFECT", side->transport.ffe1_handle,
                           payload, length);
        if (ok) {
            vTaskDelay(pdMS_TO_TICKS(DEPENDENT_DELAY_MS));
            ok = sp624e_build_white(original.white, payload, sizeof(payload), &length) == 0 &&
                 execute_write(side, "RESTORE_WHITE_LEVEL", side->transport.ffe1_handle,
                               payload, length);
        }
    } else if (original.effect == SP624E_EFFECT_SOLID) {
        ok = sp624e_build_effect(SP624E_EFFECT_SOLID, payload, sizeof(payload), &length) == 0 &&
             execute_write(side, "RESTORE_SOLID_EFFECT", side->transport.ffe1_handle,
                           payload, length);
        if (ok) {
            vTaskDelay(pdMS_TO_TICKS(DEPENDENT_DELAY_MS));
            ok = sp624e_build_rgb(original.red, original.green, original.blue,
                                  original.brightness, payload, sizeof(payload), &length) == 0 &&
                 execute_write(side, "RESTORE_RGB", side->transport.ffe1_handle,
                               payload, length);
        }
    } else {
        ok = sp624e_build_rgb(original.red, original.green, original.blue,
                              original.brightness, payload, sizeof(payload), &length) == 0 &&
             execute_write(side, "RESTORE_RGB", side->transport.ffe1_handle, payload, length);
        if (ok) {
            vTaskDelay(pdMS_TO_TICKS(DEPENDENT_DELAY_MS));
            ok = sp624e_build_effect(original.effect, payload, sizeof(payload), &length) == 0 &&
                 execute_write(side, "RESTORE_EFFECT", side->transport.ffe1_handle,
                               payload, length);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    ok = ok && query_state(side) && sp624e_visual_equivalent(&original, &side->observed);
    ESP_LOGI(TAG_TEST, "%s original restore=%s", sp624e_side_name(side->side),
             ok ? "PASS" : "FAIL");
    return ok;
}

static bool enqueue(side_context_t *side, sp624e_command_type_t type,
                    uint32_t generation, bool coalesce)
{
    sp624e_command_t command = {.generation = generation, .type = type,
                                .requires_verification = type == SP624E_COMMAND_RECONCILE};
    bool ok = sp624e_command_queue_push(&side->queue, &command, coalesce);
    if (ok) {
        if (type == SP624E_COMMAND_RECONCILE) side->reconcile_queued = true;
        xTaskNotifyGive(side->worker_task);
        sp624e_connection_metrics_t metrics;
        sp624e_metrics_get_side(side->side, &metrics);
        metrics.queue_depth = (uint32_t)sp624e_command_queue_depth(&side->queue);
        if (metrics.queue_depth > metrics.max_queue_depth) metrics.max_queue_depth = metrics.queue_depth;
        metrics.coalesced_commands = sp624e_command_queue_coalesced_count(&side->queue);
        sp624e_metrics_set_side(side->side, &metrics);
        lock();
        if (metrics.queue_depth > s_animation_max_queue_depth) {
            s_animation_max_queue_depth = metrics.queue_depth;
        }
        unlock();
    } else {
        ESP_LOGE(TAG_CMD, "%s queue full command=%s", sp624e_side_name(side->side),
                 sp624e_command_type_name(type));
    }
    return ok;
}

static void finish_pipeline_ready(side_context_t *side, const char *reason)
{
    side->reconcile_queued = false;
    ble_registry_mark_ready_for_control(side->transport.entry);
    ble_connection_manager_set_stage(side->side, BLE_CONNECTION_READY, reason);
    side->health_due_ms = now_ms() + APP_BLE_HEALTH_INTERVAL_MS +
                          (side->side == SP624E_SIDE_RIGHT ? 500 : 0);
}

static void worker_task(void *arg)
{
    side_context_t *side = arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        sp624e_command_t command;
        uint32_t stale = 0;
        uint32_t generation;
        lock(); generation = s_desired.generation; unlock();
        while (sp624e_command_queue_pop(&side->queue, generation, &command, &stale)) {
            if (stale > 0) {
                sp624e_connection_metrics_t metrics;
                sp624e_metrics_get_side(side->side, &metrics);
                metrics.stale_commands_discarded += stale;
                sp624e_metrics_set_side(side->side, &metrics);
                ESP_LOGW(TAG_CMD, "%s discarded stale=%" PRIu32,
                         sp624e_side_name(side->side), stale);
            }
            bool ok = true;
            if (command.session_id != 0) {
                lock();
                bool current_animation = s_animation_active &&
                    command.session_id == s_animation_generation;
                unlock();
                if (!current_animation) continue;
            }
            if (command.type == SP624E_COMMAND_ENABLE_NOTIFICATIONS) {
                ble_connection_manager_set_stage(side->side, BLE_CONNECTION_SUBSCRIBING,
                                                  "enabling CCCD");
                ok = enable_notifications(side);
                if (ok) {
                    ble_connection_manager_set_stage(side->side, BLE_CONNECTION_QUERYING_STATE,
                                                      "subscription complete");
                    ok = query_state(side);
                }
                if (ok) {
                    if (!side->original_valid) {
                        side->original = side->observed;
                        side->original_valid = side->observed.valid;
                    }
                    side->initial_query_done = true;
                    ble_connection_manager_set_stage(side->side, BLE_CONNECTION_SYNC_PENDING,
                                                      "state query valid; strict sync pending");
                }
            } else if (command.type == SP624E_COMMAND_STATE_QUERY) {
                ok = query_state(side);
                if (ok) {
                    sp624e_desired_state_t desired;
                    bool authority;
                    lock(); desired = s_desired; authority = s_authority; unlock();
                    if (authority && desired.valid &&
                        !sp624e_reconciler_matches(&desired, &side->observed) &&
                        now_ms() >= side->external_cooldown_until_ms) {
                        side->external_cooldown_until_ms = now_ms() + EXTERNAL_COOLDOWN_MS;
                        if (ble_connection_manager_both_ready()) {
                            ESP_LOGW(TAG_STATE,
                                     "%s EXTERNAL_STATE_CHANGE generation=%" PRIu32,
                                     sp624e_side_name(side->side), desired.generation);
                            maybe_schedule_group_reconcile();
                        } else {
                            ESP_LOGI(TAG_STATE,
                                     "%s reconcile deferred generation=%" PRIu32
                                     " reason=strict sync peer offline",
                                     sp624e_side_name(side->side), desired.generation);
                            set_group_state(SP624E_GROUP_DEGRADED,
                                            "strict sync: peer offline; no one-sided write");
                        }
                    }
                }
            } else if (command.type == SP624E_COMMAND_RECONCILE) {
                ble_connection_manager_set_stage(side->side, BLE_CONNECTION_RECONCILING,
                                                  "desired generation pending");
                ok = reconcile(side, command.generation);
                if (ok) finish_pipeline_ready(side, "desired state verified");
            } else if (command.type == SP624E_COMMAND_RESTORE) {
                ok = restore_original(side);
                if (ok) finish_pipeline_ready(side, "original state restored");
            } else if (command.type == SP624E_COMMAND_EFFECT ||
                       command.type == SP624E_COMMAND_RGB ||
                       command.type == SP624E_COMMAND_BRIGHTNESS) {
                ok = execute_write(side, sp624e_command_type_name(command.type),
                                   side->transport.ffe1_handle,
                                   command.payload, command.payload_len);
                if (ok && command.session_id != 0 &&
                    (command.type == SP624E_COMMAND_RGB ||
                     command.type == SP624E_COMMAND_BRIGHTNESS)) {
                    lock(); s_animation_frames_sent[side->side]++; unlock();
                }
            }
            if (!ok) {
                side->reconcile_queued = false;
                if (!side_is_connected(side)) {
                    ESP_LOGI(TAG_CMD, "%s in-flight command canceled by disconnect",
                             sp624e_side_name(side->side));
                } else if (side->transport.cached_handles &&
                           !side->initial_query_done) {
                    set_group_state(SP624E_GROUP_DEGRADED,
                                    "cached GATT path failed; rediscovering");
                    side->transport.cached_handles = false;
                    ble_connection_manager_on_fast_gatt_failed(
                        side->side, side->write_status);
                } else {
                    set_group_state(SP624E_GROUP_DEGRADED,
                                    "side command/pipeline failed");
                    ble_connection_manager_mark_unhealthy(side->side,
                                                           "pipeline command failed");
                }
            }
            lock(); generation = s_desired.generation; unlock();
        }
        if (stale > 0) {
            sp624e_connection_metrics_t metrics;
            sp624e_metrics_get_side(side->side, &metrics);
            metrics.stale_commands_discarded += stale;
            sp624e_metrics_set_side(side->side, &metrics);
        }
    }
}

static void process_notification(const notify_packet_t *packet)
{
    side_context_t *side = side_by_handle(packet->conn_handle);
    if (side == NULL || packet->attr_handle != side->transport.ffe1_handle) return;
    const uint8_t *message = NULL;
    size_t message_length = 0;
    sp624e_protocol_result_t result = sp624e_reassembly_push(
        &side->reassembly, packet->data, packet->length, &message, &message_length);
    if (result == SP624E_PROTOCOL_INCOMPLETE) return;
    if (result != SP624E_PROTOCOL_OK) {
        ESP_LOGW(TAG_NOTIFY, "%s reassembly rejected result=%d",
                 sp624e_side_name(side->side), result);
        return;
    }
    sp624e_light_state_t parsed;
    result = sp624e_state_parse(message, message_length, now_ms(), &parsed);
    if (result != SP624E_PROTOCOL_OK) {
        ESP_LOGW(TAG_NOTIFY, "%s state parse rejected result=%d",
                 sp624e_side_name(side->side), result);
        return;
    }
    side->observed = parsed;
    side->state_sequence++;
    ble_connection_manager_on_valid_state(side->side);
    ESP_LOGI(TAG_NOTIFY,
             "%s STATE power=%d effect=0x%02X mode=%u rgb=%u,%u,%u brightness=%u speed=%u",
             sp624e_side_name(side->side), parsed.power, parsed.effect, parsed.mode,
             parsed.red, parsed.green, parsed.blue, parsed.brightness, parsed.speed);
    xSemaphoreGive(side->state_done);
}

static bool both_initial_queries_done(void)
{
    return s_sides[0].initial_query_done && s_sides[1].initial_query_done;
}

static void evaluate_boot_state(void)
{
    if (s_boot_evaluated || !both_initial_queries_done()) return;
    s_boot_evaluated = true;
    if (s_desired_was_persisted) {
        bool left_match = sp624e_reconciler_matches(&s_desired, &s_sides[0].observed);
        bool right_match = sp624e_reconciler_matches(&s_desired, &s_sides[1].observed);
        if (left_match && right_match) {
            s_sides[0].last_verified_generation = s_desired.generation;
            s_sides[1].last_verified_generation = s_desired.generation;
            s_group_metrics.verified_generation[0] = s_desired.generation;
            s_group_metrics.verified_generation[1] = s_desired.generation;
            s_authority = true;
            finish_pipeline_ready(&s_sides[0], "persisted desired verified by query");
            finish_pipeline_ready(&s_sides[1], "persisted desired verified by query");
            set_group_state(SP624E_GROUP_SYNCED, "persisted desired matches both; no boot writes");
        } else if (s_restore_on_boot) {
            s_authority = true;
            set_group_state(SP624E_GROUP_RECONCILING, "restore_on_boot enabled");
        } else {
            set_group_state(SP624E_GROUP_UNSYNCED,
                            "persisted desired differs; restore_on_boot=false, no writes");
        }
    } else if (sp624e_visual_equivalent(&s_sides[0].observed, &s_sides[1].observed)) {
        sp624e_desired_from_observed(&s_desired, &s_sides[0].observed, 1);
        s_sides[0].last_verified_generation = s_desired.generation;
        s_sides[1].last_verified_generation = s_desired.generation;
        s_group_metrics.desired_generation = s_desired.generation;
        s_group_metrics.verified_generation[0] = s_desired.generation;
        s_group_metrics.verified_generation[1] = s_desired.generation;
        s_authority = true;
        s_persist_due_ms = now_ms() + PERSIST_DEBOUNCE_MS;
        finish_pipeline_ready(&s_sides[0], "equivalent observed state adopted and verified");
        finish_pipeline_ready(&s_sides[1], "equivalent observed state adopted and verified");
        set_group_state(SP624E_GROUP_SYNCED, "equivalent observed states adopted without writes");
    } else {
        set_group_state(SP624E_GROUP_UNSYNCED,
                        "LEFT/RIGHT observed states differ; no arbitrary selection");
    }
}

static void maybe_schedule_group_reconcile(void)
{
    if (s_hold_strict_dispatch || s_animation_active || !s_authority || !s_desired.valid ||
        !ble_connection_manager_both_sync_eligible()) return;
    bool pending = false;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        side_context_t *side = &s_sides[i];
        if (side->last_verified_generation != s_desired.generation && !side->reconcile_queued) {
            sp624e_command_queue_discard_older(&side->queue, s_desired.generation);
            enqueue(side, SP624E_COMMAND_RECONCILE, s_desired.generation, true);
            pending = true;
        }
    }
    if (pending) set_group_state(SP624E_GROUP_RECONCILING, "strict group generation dispatch");
}

static void update_group_completion(void)
{
    bool both_ready = ble_connection_manager_both_ready();
    lock();
    if (!s_desired.valid) {
        unlock();
        return;
    }
    s_group_metrics.desired_generation = s_desired.generation;
    s_group_metrics.verified_generation[0] = s_sides[0].last_verified_generation;
    s_group_metrics.verified_generation[1] = s_sides[1].last_verified_generation;
    bool synced = sp624e_group_generation_is_synced(
        s_desired.generation,
        s_sides[0].last_verified_generation,
        s_sides[1].last_verified_generation,
        both_ready);
    bool schedule_persistence = synced && sp624e_desired_needs_persistence(
        &s_desired, s_persisted_generation, s_temporary_desired,
        s_persist_due_ms > 0);
    if (schedule_persistence) s_persist_due_ms = now_ms() + PERSIST_DEBOUNCE_MS;
    unlock();
    if (synced) {
        set_group_state(SP624E_GROUP_SYNCED, "both sides verified desired generation");
    }
    lock();
    sp624e_group_metrics_t metrics = s_group_metrics;
    unlock();
    sp624e_metrics_set_group(&metrics);
}

static uint32_t request_rgb(uint8_t red, uint8_t green, uint8_t blue,
                            uint8_t brightness, bool temporary)
{
    lock();
    sp624e_desired_set_rgb(&s_desired, red, green, blue, brightness);
    s_authority = true;
    s_temporary_desired = temporary;
    s_group_metrics.desired_generation = s_desired.generation;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        if (sp624e_command_queue_discard_older(&s_sides[i].queue,
                                               s_desired.generation) > 0) {
            s_sides[i].reconcile_queued = false;
        }
    }
    uint32_t generation = s_desired.generation;
    unlock();
    ESP_LOGD(TAG, "DESIRED_STATE_CHANGED generation=%" PRIu32
             " RGB=%u,%u,%u brightness=%u temporary=%d", generation,
             red, green, blue, brightness, temporary);
    if (ble_connection_manager_both_ready()) {
        maybe_schedule_group_reconcile();
    } else {
        set_group_state(ble_connection_manager_any_fast_recovery() ?
                        SP624E_GROUP_POWER_CYCLE_RECOVERY : SP624E_GROUP_DEGRADED,
                        "strict sync: desired retained until both READY");
    }
    return generation;
}

static uint32_t request_white(uint8_t level, bool temporary)
{
    lock();
    sp624e_desired_set_white(&s_desired, level);
    s_authority = true;
    s_temporary_desired = temporary;
    s_group_metrics.desired_generation = s_desired.generation;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        if (sp624e_command_queue_discard_older(&s_sides[i].queue,
                                               s_desired.generation) > 0) {
            s_sides[i].reconcile_queued = false;
        }
    }
    uint32_t generation = s_desired.generation;
    unlock();
    ESP_LOGI(TAG, "DESIRED_STATE_CHANGED generation=%" PRIu32
             " WHITE=%u temporary=%d", generation, level, temporary);
    if (ble_connection_manager_both_ready()) {
        maybe_schedule_group_reconcile();
    } else {
        set_group_state(ble_connection_manager_any_fast_recovery() ?
                        SP624E_GROUP_POWER_CYCLE_RECOVERY : SP624E_GROUP_DEGRADED,
                        "strict sync: desired retained until both READY");
    }
    return generation;
}

static void process_group_api_requests(void)
{
    group_api_request_t request;
    while (xQueueReceive(s_group_api_queue, &request, 0) == pdTRUE) {
        sp624e_group_api_result_t result = SP624E_GROUP_API_OK;
        uint32_t generation = 0;
        if (request.type == GROUP_API_SET_RGB) {
            generation = request_rgb(request.red, request.green, request.blue,
                                     request.level, false);
        } else if (request.type == GROUP_API_SET_WHITE) {
            if (!s_white_available) {
                result = SP624E_GROUP_API_UNSUPPORTED;
            } else {
                generation = request_white(request.level, false);
            }
        } else if (request.type == GROUP_API_RESYNC) {
            lock();
            s_authority = true;
            generation = s_desired.generation;
            for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
                s_sides[i].last_verified_generation = 0;
            }
            unlock();
            maybe_schedule_group_reconcile();
        }
        group_api_response_t response = {
            .id = request.id, .result = result, .generation = generation,
        };
        if (xQueueSend(s_group_api_response_queue, &response, 0) != pdTRUE) {
            runtime_health_increment(&s_group_api_response_drops);
            ESP_LOGE(TAG, "Group API response queue full id=%" PRIu32, request.id);
        }
    }
}

static void print_status(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "SP624E RUNTIME STATUS");
    ESP_LOGI(TAG, "GROUP state=%s generation=%" PRIu32,
             sp624e_group_state_name(s_group_state), s_desired.generation);
    for (sp624e_side_t i = SP624E_SIDE_LEFT; i < SP624E_SIDE_COUNT; ++i) {
        ble_connection_manager_status_t status;
        sp624e_connection_metrics_t metrics;
        ble_connection_manager_get_status(i, &status);
        sp624e_metrics_get_side(i, &metrics);
        int64_t uptime = status.connected && metrics.last_connect_timestamp_ms > 0 ?
            now_ms() - metrics.last_connect_timestamp_ms : 0;
        ESP_LOGI(TAG, "%s state=%s connected=%s RSSI=%d uptime=%" PRId64
                 "ms queue=%u verified=%" PRIu32 " reconnects=%" PRIu32,
                 sp624e_side_name(i), ble_connection_state_name(status.state),
                 status.connected ? "YES" : "NO", metrics.last_rssi, uptime,
                 metrics.queue_depth, s_sides[i].last_verified_generation,
                 metrics.reconnect_success_count);
    }
    {
        /* Interior is intent only: never reported as synced or confirmed. */
        interior_light_snapshot_t interior;
        interior_light_get_snapshot(&interior);
        ESP_LOGI(TAG,
                 "INTERIOR state=%s desired=%u,%u,%u attempted=%s last_write_age=%" PRId64
                 "ms writes=%" PRIu32 " connect_fail=%" PRIu32 " backoff=%" PRIu32 "ms",
                 interior_light_state_name(interior.state), interior.desired.red,
                 interior.desired.green, interior.desired.blue,
                 interior.last_attempt_valid ? "yes" : "none",
                 interior.last_write_ms > 0 ? now_ms() - interior.last_write_ms : -1,
                 interior.write_attempts, interior.connect_failures, interior.backoff_ms);
    }
    ESP_LOGI(TAG, "HEAP free=%" PRIu32 " minimum=%" PRIu32,
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "========================================");
}

static void print_states(void)
{
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        const sp624e_light_state_t *state = &s_sides[i].observed;
        ESP_LOGI(TAG_STATE, "%s valid=%d power=%d effect=0x%02X mode=%u RGB=%u,%u,%u "
                 "brightness=%u white=%u speed=%u applied=%" PRIu32 " verified=%" PRIu32,
                 sp624e_side_name((sp624e_side_t)i), state->valid, state->power,
                 state->effect, state->mode, state->red, state->green, state->blue,
                 state->brightness, state->white, state->speed,
                 s_sides[i].last_applied_generation,
                 s_sides[i].last_verified_generation);
    }
}

static void print_metrics(void)
{
    for (sp624e_side_t i = SP624E_SIDE_LEFT; i < SP624E_SIDE_COUNT; ++i) {
        sp624e_connection_metrics_t m;
        sp624e_metrics_get_side(i, &m);
        ESP_LOGI(TAG, "%s METRICS connect_attempts=%" PRIu32 " connected=%" PRIu32
                 " disconnects=%" PRIu32 " reconnect_ok=%" PRIu32
                 " reconnect_fail=%" PRIu32 " reason=0x%X backoff=%" PRIu32
                 "ms queries=%" PRIu32 " query_fail=%" PRIu32
                 " reconciles=%" PRIu32 " reconcile_fail=%" PRIu32
                 " commands=%" PRIu32 " command_fail=%" PRIu32
                 " stale=%" PRIu32 " queue=%" PRIu32,
                 sp624e_side_name(i), m.connect_attempts, m.successful_connections,
                 m.disconnect_count, m.reconnect_success_count, m.reconnect_failure_count,
                 m.last_disconnect_reason, m.current_backoff_ms, m.state_query_count,
                 m.state_query_failures, m.reconcile_count, m.reconcile_failures,
                 m.command_count, m.command_failures, m.stale_commands_discarded,
                 m.queue_depth);
        ESP_LOGI(TAG,
                 "%s RECOVERY power_suspected=%" PRIu32 " fast=%" PRIu32
                 " pass=%" PRIu32 " fail=%" PRIu32 " 0x3e=%" PRIu32
                 " supervision_disconnects=%" PRIu32 " dual=%" PRIu32
                 " detect_avg/max=%" PRIu32 "/%" PRIu32
                 " adv_connect_avg/max=%" PRIu32 "/%" PRIu32
                 " adv_ready_avg/max=%" PRIu32 "/%" PRIu32
                 " total_avg/max=%" PRIu32 "/%" PRIu32 "ms",
                 sp624e_side_name(i), m.power_cycle_suspected_count,
                 m.fast_recovery_count, m.fast_recovery_success,
                 m.fast_recovery_failure, m.connection_0x3e_count,
                 m.supervision_timeout_disconnects, m.dual_disconnect_count,
                 m.avg_disconnect_detection_ms, m.max_disconnect_detection_ms,
                 m.avg_adv_to_connect_ms, m.max_adv_to_connect_ms,
                 m.avg_adv_to_ready_ms, m.max_adv_to_ready_ms,
                 m.avg_disconnect_to_synced_ms, m.max_disconnect_to_synced_ms);
    }
    ESP_LOGI(TAG, "GROUP METRICS sync=%" PRIu32 " desync=%" PRIu32
             " degraded=%" PRIu32 " max_desync=%" PRIu64 "ms desired=%" PRIu32
             " verified=%" PRIu32 "/%" PRIu32,
             s_group_metrics.group_sync_count, s_group_metrics.group_desync_count,
             s_group_metrics.group_degraded_count, s_group_metrics.max_desync_duration_ms,
             s_group_metrics.desired_generation, s_group_metrics.verified_generation[0],
             s_group_metrics.verified_generation[1]);
}

static bool parse_side(const char *text, sp624e_side_t *side)
{
    if (strcasecmp(text, "left") == 0) { *side = SP624E_SIDE_LEFT; return true; }
    if (strcasecmp(text, "right") == 0) { *side = SP624E_SIDE_RIGHT; return true; }
    return false;
}

static void restore_both_original(void)
{
    enqueue(&s_sides[0], SP624E_COMMAND_RESTORE, 0, false);
    enqueue(&s_sides[1], SP624E_COMMAND_RESTORE, 0, false);
}

static bool queue_test(test_request_type_t type, sp624e_side_t side, uint32_t duration)
{
    test_request_t request = {.type = type, .side = side, .duration_seconds = duration};
    if (xQueueSend(s_test_queue, &request, 0) != pdTRUE) {
        ESP_LOGE(TAG_TEST, "test queue busy");
        return false;
    }
    return true;
}

static void handle_console_line(char *line)
{
    char *save = NULL;
    char *command = strtok_r(line, " \t", &save);
    if (command == NULL) return;
    if (strcasecmp(command, "status") == 0) {
        print_status();
    } else if (strcasecmp(command, "state") == 0) {
        print_states();
    } else if (strcasecmp(command, "metrics") == 0) {
        print_metrics();
    } else if (strcasecmp(command, "rgb") == 0) {
        char *values[4];
        for (size_t i = 0; i < 4; ++i) values[i] = strtok_r(NULL, " \t", &save);
        if (values[0] == NULL || values[1] == NULL || values[2] == NULL || values[3] == NULL) {
            ESP_LOGE(TAG, "usage: rgb <r> <g> <b> <brightness>");
            return;
        }
        long r = strtol(values[0], NULL, 10), g = strtol(values[1], NULL, 10);
        long b = strtol(values[2], NULL, 10), level = strtol(values[3], NULL, 10);
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 ||
            level < 0 || level > 255) {
            ESP_LOGE(TAG, "rgb values must be 0..255");
            return;
        }
        request_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)level, false);
    } else if (strcasecmp(command, "resync") == 0) {
        s_authority = true;
        for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
            s_sides[i].last_verified_generation = 0;
        }
        maybe_schedule_group_reconcile();
    } else if (strcasecmp(command, "disconnect") == 0 ||
               strcasecmp(command, "reconnect") == 0) {
        char *value = strtok_r(NULL, " \t", &save);
        sp624e_side_t side;
        if (value == NULL || !parse_side(value, &side)) {
            ESP_LOGE(TAG, "usage: %s left|right", command);
            return;
        }
        if (tolower((unsigned char)command[0]) == 'd') {
            ble_connection_manager_request_disconnect(side);
        } else {
            ble_connection_manager_request_reconnect(side);
        }
    } else if (strcasecmp(command, "test-reconnect") == 0) {
        char *value = strtok_r(NULL, " \t", &save);
        sp624e_side_t side;
        if (value == NULL || !parse_side(value, &side)) {
            ESP_LOGE(TAG_TEST, "usage: test-reconnect left|right");
            return;
        }
        queue_test(TEST_REQUEST_RECONNECT, side, 0);
    } else if (strcasecmp(command, "test-sync") == 0) {
        queue_test(TEST_REQUEST_SYNC, SP624E_SIDE_LEFT, 0);
    } else if (strcasecmp(command, "test-stress") == 0) {
        queue_test(TEST_REQUEST_STRESS, SP624E_SIDE_LEFT, 0);
    } else if (strcasecmp(command, "test-midfail") == 0) {
        queue_test(TEST_REQUEST_MIDFAIL, SP624E_SIDE_RIGHT, 0);
    } else if (strcasecmp(command, "test-stability") == 0) {
        char *value = strtok_r(NULL, " \t", &save);
        uint32_t seconds = value == NULL ? APP_BLE_STABILITY_TEST_SECONDS :
                           (uint32_t)strtoul(value, NULL, 10);
        if (seconds == 0) seconds = APP_BLE_STABILITY_TEST_SECONDS;
        queue_test(TEST_REQUEST_STABILITY, SP624E_SIDE_LEFT, seconds);
    } else if (strcasecmp(command, "test-white") == 0) {
        queue_test(TEST_REQUEST_WHITE, SP624E_SIDE_LEFT, 0);
    } else if (strcasecmp(command, "white-confirm") == 0) {
        char *value = strtok_r(NULL, " \t", &save);
        bool enable = value != NULL && strcasecmp(value, "yes") == 0;
        if (enable && !s_white_test_pending_confirmation) {
            ESP_LOGE(TAG_TEST, "WHITE_CONFIRM rejected: run test-white successfully first");
            return;
        }
        if (value == NULL || (!enable && strcasecmp(value, "no") != 0)) {
            ESP_LOGE(TAG_TEST, "usage: white-confirm yes|no");
            return;
        }
        esp_err_t err = preset_manager_set_white_available(enable);
        if (err == ESP_OK) {
            lock(); s_white_available = enable; unlock();
            s_white_test_pending_confirmation = false;
        }
        ESP_LOGI(TAG_TEST, "WHITE_CAPABILITY enabled=%d result=%s", enable,
                 esp_err_to_name(err));
    } else if (strcasecmp(command, "restore") == 0) {
        queue_test(TEST_REQUEST_RESTORE, SP624E_SIDE_LEFT, 0);
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", command);
    }
}

static bool wait_group_synced(uint32_t timeout_ms)
{
    int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        if (s_group_state == SP624E_GROUP_SYNCED && ble_connection_manager_both_ready()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static bool wait_observed(const sp624e_light_state_t snapshots[SP624E_SIDE_COUNT],
                          uint32_t timeout_ms)
{
    int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        if (sp624e_visual_equivalent(&snapshots[0], &s_sides[0].observed) &&
            sp624e_visual_equivalent(&snapshots[1], &s_sides[1].observed)) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static void restore_test_snapshot(const sp624e_light_state_t snapshots[SP624E_SIDE_COUNT],
                                  const sp624e_desired_state_t *prior_desired,
                                  bool prior_authority)
{
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        s_sides[i].original = snapshots[i];
        s_sides[i].original_valid = snapshots[i].valid;
        enqueue(&s_sides[i], SP624E_COMMAND_RESTORE, 0, false);
    }
    bool restored = wait_observed(snapshots, 15000);
    lock();
    s_desired = *prior_desired;
    s_authority = prior_authority;
    s_temporary_desired = false;
    if (restored && prior_desired->valid &&
        sp624e_reconciler_matches(prior_desired, &s_sides[0].observed) &&
        sp624e_reconciler_matches(prior_desired, &s_sides[1].observed)) {
        s_sides[0].last_verified_generation = prior_desired->generation;
        s_sides[1].last_verified_generation = prior_desired->generation;
        s_group_metrics.verified_generation[0] = prior_desired->generation;
        s_group_metrics.verified_generation[1] = prior_desired->generation;
    }
    unlock();
    if (restored) update_group_completion();
    ESP_LOGI(TAG_TEST, "RESTORE_ORIGINAL_STATE result=%s", restored ? "PASS" : "FAIL");
}

static bool run_reconnect_cycle(sp624e_side_t side, unsigned cycle)
{
    if (!wait_group_synced(30000)) {
        ESP_LOGE(TAG_TEST, "Cycle %u side=%s precondition=FAIL", cycle,
                 sp624e_side_name(side));
        return false;
    }
    sp624e_connection_metrics_t before;
    sp624e_metrics_get_side(side, &before);
    int64_t started = now_ms();
    ble_connection_manager_request_power_cycle_test(side);
    bool saw_disconnect = false;
    int64_t deadline = started + 30000;
    while (now_ms() < deadline) {
        ble_connection_manager_status_t status;
        ble_connection_manager_get_status(side, &status);
        if (!status.connected) saw_disconnect = true;
        if (saw_disconnect && wait_group_synced(50)) break;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    int64_t finished = now_ms();
    sp624e_connection_metrics_t after;
    sp624e_metrics_get_side(side, &after);
    bool passed = saw_disconnect && s_group_state == SP624E_GROUP_SYNCED &&
                  ble_connection_manager_is_ready(side) && finished - started <= 30000;
    int64_t connected_ms = after.last_connect_timestamp_ms > started ?
                           after.last_connect_timestamp_ms - started : -1;
    ESP_LOGI(TAG_TEST,
             "Cycle %u Side=%s DisconnectDetected=%s Connected=%" PRId64
             "ms READY_SYNCED=%" PRId64 "ms Attempts=%" PRIu32
             " Commands=%" PRIu32 " Verification=%s Result=%s",
             cycle, sp624e_side_name(side), saw_disconnect ? "YES" : "NO",
             connected_ms, finished - started,
             after.connect_attempts - before.connect_attempts,
             after.command_count - before.command_count,
             passed ? "PASS" : "FAIL", passed ? "PASS" : "FAIL");
    return passed;
}

static void run_sync_test(void)
{
    sp624e_light_state_t snapshots[SP624E_SIDE_COUNT] = {
        s_sides[0].observed, s_sides[1].observed
    };
    sp624e_desired_state_t prior = s_desired;
    bool prior_authority = s_authority;
    ESP_LOGI(TAG_TEST, "TEST_SYNC_START temporary=1 color=GREEN");
    request_rgb(0, 255, 0, 64, true);
    bool passed = wait_group_synced(15000) &&
                  s_sides[0].observed.red == 0 && s_sides[0].observed.green == 255 &&
                  s_sides[1].observed.red == 0 && s_sides[1].observed.green == 255;
    ESP_LOGI(TAG_TEST, "TEST_SYNC_RESULT=%s", passed ? "PASS" : "FAIL");
    restore_test_snapshot(snapshots, &prior, prior_authority);
}

static void run_stress_test(void)
{
    unsigned passed = 0;
    ESP_LOGI(TAG_TEST, "ALTERNATING_RECONNECT_STRESS start cycles=10");
    for (unsigned cycle = 1; cycle <= 10; ++cycle) {
        sp624e_side_t side = (cycle & 1u) ? SP624E_SIDE_LEFT : SP624E_SIDE_RIGHT;
        if (run_reconnect_cycle(side, cycle)) passed++;
        else break;
        if (cycle < 10) {
            /* Let READY remain stable long enough to reset per-side backoff. */
            vTaskDelay(pdMS_TO_TICKS(10500));
        }
    }
    ESP_LOGI(TAG_TEST, "ALTERNATING_RECONNECT_STRESS pass=%u fail=%u result=%s",
             passed, 10 - passed, passed == 10 ? "PASS" : "FAIL");
}

static void run_midfail_test(void)
{
    if (!wait_group_synced(30000)) {
        ESP_LOGE(TAG_TEST, "MID_COMMAND_FAILURE precondition=FAIL");
        return;
    }
    sp624e_light_state_t snapshots[SP624E_SIDE_COUNT] = {
        s_sides[0].observed, s_sides[1].observed
    };
    sp624e_desired_state_t prior = s_desired;
    bool prior_authority = s_authority;
    lock();
    s_hold_strict_dispatch = true;
    sp624e_desired_set_rgb(&s_desired, 255, 0, 0, 64);
    s_authority = true;
    s_temporary_desired = true;
    uint32_t generation = s_desired.generation;
    s_group_metrics.desired_generation = generation;
    unlock();
    set_group_state(SP624E_GROUP_RECONCILING, "mid-command controlled test");
    enqueue(&s_sides[SP624E_SIDE_LEFT], SP624E_COMMAND_RECONCILE, generation, false);
    int64_t deadline = now_ms() + 10000;
    while (now_ms() < deadline &&
           s_sides[SP624E_SIDE_LEFT].last_verified_generation != generation) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    int64_t degraded_start = now_ms();
    ble_connection_manager_request_power_cycle_test(SP624E_SIDE_RIGHT);
    bool degraded = false;
    deadline = now_ms() + 3000;
    while (now_ms() < deadline) {
        if (s_group_state == SP624E_GROUP_DEGRADED) { degraded = true; break; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_hold_strict_dispatch = false;
    bool synced = wait_group_synced(30000);
    ESP_LOGI(TAG_TEST,
             "MID_COMMAND_FAILURE side=RIGHT desired=RED degraded=%s reconnect=%s "
             "reconcile=%s synced=%s desync=%" PRId64 "ms result=%s",
             degraded ? "YES" : "NO", synced ? "YES" : "NO",
             synced ? "YES" : "NO", synced ? "YES" : "NO",
             now_ms() - degraded_start, degraded && synced ? "PASS" : "FAIL");
    restore_test_snapshot(snapshots, &prior, prior_authority);
}

static void run_stability_test(uint32_t seconds)
{
    uint32_t initial_heap = esp_get_free_heap_size();
    uint32_t initial_min = esp_get_minimum_free_heap_size();
    sp624e_connection_metrics_t initial[SP624E_SIDE_COUNT];
    sp624e_metrics_get_side(SP624E_SIDE_LEFT, &initial[0]);
    sp624e_metrics_get_side(SP624E_SIDE_RIGHT, &initial[1]);
    bool passed = wait_group_synced(30000);
    int64_t started = now_ms();
    ESP_LOGI(TAG_TEST, "STABILITY_START duration=%" PRIu32 "s heap=%" PRIu32,
             seconds, initial_heap);
    while (passed && now_ms() - started < (int64_t)seconds * 1000) {
        if (!ble_connection_manager_both_ready() || s_group_state != SP624E_GROUP_SYNCED) {
            passed = false;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    sp624e_connection_metrics_t final[SP624E_SIDE_COUNT];
    sp624e_metrics_get_side(SP624E_SIDE_LEFT, &final[0]);
    sp624e_metrics_get_side(SP624E_SIDE_RIGHT, &final[1]);
    uint32_t final_heap = esp_get_free_heap_size();
    uint32_t minimum_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG_TEST,
             "STABILITY_RESULT=%s duration=%" PRId64 "ms initial_heap=%" PRIu32
             " final_heap=%" PRIu32 " min_start=%" PRIu32 " min_end=%" PRIu32
             " LEFT_disconnects=%" PRIu32 " RIGHT_disconnects=%" PRIu32
             " health_queries=%" PRIu32 "/%" PRIu32,
             passed ? "PASS" : "FAIL", now_ms() - started, initial_heap, final_heap,
             initial_min, minimum_heap,
             final[0].disconnect_count - initial[0].disconnect_count,
             final[1].disconnect_count - initial[1].disconnect_count,
             final[0].state_query_count - initial[0].state_query_count,
             final[1].state_query_count - initial[1].state_query_count);
}

static bool run_white_on_side(side_context_t *side, uint8_t level)
{
    sp624e_light_state_t snapshot = side->observed;
    side->original = snapshot;
    side->original_valid = snapshot.valid;
    uint8_t payload[SP624E_COMMAND_MAX_LEN];
    size_t length = 0;
    ESP_LOGI(TAG_TEST, "WHITE_INDIVIDUAL side=%s stage=START hold=15s level=%u",
             sp624e_side_name(side->side), level);
    bool ok = snapshot.valid &&
              sp624e_build_effect(SP624E_EFFECT_WHITE, payload, sizeof(payload), &length) == 0 &&
              execute_write(side, "TEST_WHITE_EFFECT", side->transport.ffe1_handle,
                            payload, length);
    if (ok) {
        vTaskDelay(pdMS_TO_TICKS(DEPENDENT_DELAY_MS));
        ok = sp624e_build_white(level, payload, sizeof(payload), &length) == 0 &&
             execute_write(side, "TEST_WHITE_LEVEL", side->transport.ffe1_handle,
                           payload, length);
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    ok = ok && query_state(side) && side->observed.effect == SP624E_EFFECT_WHITE &&
         side->observed.white == level;
    if (ok) vTaskDelay(pdMS_TO_TICKS(15000));
    bool restored = restore_original(side);
    ESP_LOGI(TAG_TEST, "WHITE_INDIVIDUAL side=%s protocol=%s restore=%s result=%s",
             sp624e_side_name(side->side), ok ? "PASS" : "FAIL",
             restored ? "PASS" : "FAIL", ok && restored ? "PASS" : "FAIL");
    return ok && restored;
}

static void run_white_test(void)
{
    if (!wait_group_synced(30000)) {
        ESP_LOGE(TAG_TEST, "WHITE_TEST precondition=FAIL group_not_synced");
        return;
    }
    s_white_test_running = true;
    sp624e_light_state_t snapshots[SP624E_SIDE_COUNT] = {
        s_sides[0].observed, s_sides[1].observed
    };
    sp624e_desired_state_t prior = s_desired;
    bool prior_authority = s_authority;
    lock(); s_authority = false; unlock();
    ESP_LOGI(TAG_TEST, "WHITE_TEST_START watch LEFT then RIGHT then BOTH");
    bool left = run_white_on_side(&s_sides[SP624E_SIDE_LEFT], 96);
    bool right = left && run_white_on_side(&s_sides[SP624E_SIDE_RIGHT], 96);
    bool group_white = false;
    bool rgb_transition = false;
    if (right) {
        ESP_LOGI(TAG_TEST, "WHITE_GROUP stage=START hold=30s level=96");
        request_white(96, true);
        group_white = wait_group_synced(15000) &&
                      s_sides[0].observed.effect == SP624E_EFFECT_WHITE &&
                      s_sides[1].observed.effect == SP624E_EFFECT_WHITE &&
                      s_sides[0].observed.white == 96 && s_sides[1].observed.white == 96;
        if (group_white) vTaskDelay(pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG_TEST, "WHITE_GROUP protocol=%s", group_white ? "PASS" : "FAIL");
    }
    if (group_white) {
        ESP_LOGI(TAG_TEST, "WHITE_TO_RGB stage=START hold=10s rgb=32,96,255");
        request_rgb(32, 96, 255, 96, true);
        rgb_transition = wait_group_synced(15000) &&
                         s_sides[0].observed.effect == SP624E_EFFECT_SOLID &&
                         s_sides[1].observed.effect == SP624E_EFFECT_SOLID;
        if (rgb_transition) vTaskDelay(pdMS_TO_TICKS(10000));
    }
    restore_test_snapshot(snapshots, &prior, prior_authority);
    bool passed = left && right && group_white && rgb_transition;
    s_white_test_pending_confirmation = passed;
    s_white_test_running = false;
    ESP_LOGI(TAG_TEST, "WHITE_TEST_RESULT=%s visual_confirmation=%s",
             passed ? "PASS" : "FAIL", passed ? "REQUIRED" : "NOT_ALLOWED");
    if (passed) {
        ESP_LOGI(TAG_TEST, "Confirm observed behavior with: white-confirm yes|no");
    }
}

static void test_task(void *arg)
{
    (void)arg;
    while (true) {
        test_request_t request;
        xQueueReceive(s_test_queue, &request, portMAX_DELAY);
        if (request.type == TEST_REQUEST_SYNC) run_sync_test();
        else if (request.type == TEST_REQUEST_RECONNECT) run_reconnect_cycle(request.side, 1);
        else if (request.type == TEST_REQUEST_STRESS) run_stress_test();
        else if (request.type == TEST_REQUEST_MIDFAIL) run_midfail_test();
        else if (request.type == TEST_REQUEST_STABILITY) {
            run_stability_test(request.duration_seconds);
        } else if (request.type == TEST_REQUEST_RESTORE) {
            restore_both_original();
        } else if (request.type == TEST_REQUEST_WHITE) {
            run_white_test();
        }
    }
}

/*
 * The shared console owns UART0 so diagnostics stay reachable before the
 * SP624E pipeline is up. SP624E commands keep running on group_runtime, so
 * this handler only forwards the line to the existing console queue.
 */
static bool console_handler(char *line, void *context)
{
    (void)context;
    char forwarded[CONSOLE_LINE_MAX];
    snprintf(forwarded, sizeof(forwarded), "%s", line);
    if (xQueueSend(s_console_queue, forwarded, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Console queue full; command dropped: %s", line);
    }
    return true;
}

static void runtime_task(void *arg)
{
    (void)arg;
    bool watched = esp_task_wdt_add(NULL) == ESP_OK;
    s_white_available = preset_manager_white_available();
    ESP_LOGI(TAG, "WHITE capability=%s", s_white_available ? "ENABLED" : "DISABLED");
    esp_err_t desired_err = sp624e_desired_load(&s_desired, &s_restore_on_boot);
    if (desired_err == ESP_OK && s_desired.valid) {
        s_desired_was_persisted = true;
        s_persisted_generation = s_desired.generation;
        s_group_metrics.desired_generation = s_desired.generation;
        ESP_LOGI(TAG, "Desired State loaded generation=%" PRIu32 " restore_on_boot=%d",
                 s_desired.generation, s_restore_on_boot);
    } else if (desired_err != ESP_OK) {
        ESP_LOGW(TAG, "Desired State load ignored: %s", esp_err_to_name(desired_err));
        memset(&s_desired, 0, sizeof(s_desired));
    }

    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        enqueue(&s_sides[i], SP624E_COMMAND_ENABLE_NOTIFICATIONS, 0, false);
    }
    while (true) {
        notify_packet_t packet;
        while (xQueueReceive(s_notify_queue, &packet, 0) == pdTRUE) {
            process_notification(&packet);
        }
        char line[CONSOLE_LINE_MAX];
        while (xQueueReceive(s_console_queue, line, 0) == pdTRUE) {
            handle_console_line(line);
        }
        process_group_api_requests();
        evaluate_boot_state();
        maybe_schedule_group_reconcile();
        update_group_completion();
        /* Interior lighting mirrors the Desired State, best effort. Both calls
           are non-blocking and never touch the SP624E pipeline. */
        lock();
        sp624e_desired_state_t interior_desired = s_desired;
        unlock();
        interior_light_follow_desired(&interior_desired);
        interior_light_service();
        int64_t now = now_ms();
        for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
            side_context_t *side = &s_sides[i];
            if (!s_white_test_running && !s_animation_active &&
                ble_connection_manager_is_ready(side->side) &&
                now >= side->health_due_ms) {
                enqueue(side, SP624E_COMMAND_STATE_QUERY, 0, true);
                side->health_due_ms = now + APP_BLE_HEALTH_INTERVAL_MS;
            }
        }
        if (s_persist_due_ms > 0 && now >= s_persist_due_ms &&
            s_group_state == SP624E_GROUP_SYNCED && !s_temporary_desired) {
            lock();
            sp624e_desired_state_t desired_to_save = s_desired;
            unlock();
            esp_err_t err = sp624e_desired_save(&desired_to_save, false);
            ESP_LOGI(TAG, "Desired State persistence generation=%" PRIu32 " result=%s",
                     desired_to_save.generation, esp_err_to_name(err));
            lock();
            if (err == ESP_OK) {
                s_persisted_generation = desired_to_save.generation;
                s_persist_due_ms = s_desired.generation == desired_to_save.generation ?
                                   0 : now + PERSIST_DEBOUNCE_MS;
            } else {
                s_persist_due_ms = now + PERSIST_DEBOUNCE_MS;
            }
            unlock();
        }
        if (now - s_last_status_ms >= APP_BLE_STATUS_INTERVAL_MS) {
            print_status();
            s_last_status_ms = now;
        }
        portENTER_CRITICAL(&s_runtime_health_mux);
        s_runtime_heartbeat_ms = now_ms();
        portEXIT_CRITICAL(&s_runtime_health_mux);
        if (watched) esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void sp624e_controller_start(const sp624e_transport_t transports[2])
{
    if (s_started || transports == NULL) return;
    memset(s_sides, 0, sizeof(s_sides));
    memset(&s_group_metrics, 0, sizeof(s_group_metrics));
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    s_notify_queue = xQueueCreateStatic(NOTIFY_QUEUE_DEPTH, sizeof(notify_packet_t),
                                        s_notify_queue_buffer, &s_notify_queue_storage);
    s_console_queue = xQueueCreateStatic(CONSOLE_QUEUE_DEPTH, CONSOLE_LINE_MAX,
                                         s_console_queue_buffer, &s_console_queue_storage);
    s_test_queue = xQueueCreateStatic(TEST_QUEUE_DEPTH, sizeof(test_request_t),
                                      s_test_queue_buffer, &s_test_queue_storage);
    s_group_api_queue = xQueueCreateStatic(GROUP_API_QUEUE_DEPTH,
                                           sizeof(group_api_request_t),
                                           s_group_api_queue_buffer,
                                           &s_group_api_queue_storage);
    s_group_api_call_lock = xSemaphoreCreateMutexStatic(&s_group_api_call_lock_storage);
    s_group_api_response_queue = xQueueCreateStatic(
        GROUP_API_RESPONSE_DEPTH, sizeof(group_api_response_t),
        s_group_api_response_queue_buffer, &s_group_api_response_queue_storage);
    s_group_api_next_id = 0;
    s_group_api_timeouts = 0;
    s_group_api_busy = 0;
    s_group_api_response_drops = 0;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        side_context_t *side = &s_sides[i];
        side->side = (sp624e_side_t)i;
        side->transport = transports[i];
        side->connected = true;
        side->write_done = xSemaphoreCreateBinaryStatic(&side->write_done_storage);
        side->state_done = xSemaphoreCreateBinaryStatic(&side->state_done_storage);
        sp624e_command_queue_init(&side->queue);
        char name[16];
        snprintf(name, sizeof(name), "%s_cmd", i == 0 ? "left" : "right");
        xTaskCreate(worker_task, name, 6144, side, 6, &side->worker_task);
    }
    s_started = true;
    esp_err_t console_err = runtime_console_register(console_handler, NULL, true);
    if (console_err != ESP_OK) {
        ESP_LOGE(TAG, "Runtime serial console unavailable: %s", esp_err_to_name(console_err));
    }
    xTaskCreate(test_task, "reliability_test", 6144, NULL, 4, NULL);
    xTaskCreate(runtime_task, "group_runtime", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "SP624E CONTROLLER %s started STRICT_SYNC_MODE=%d",
             APP_FIRMWARE_VERSION, APP_STRICT_SYNC_MODE);
}

bool sp624e_controller_is_started(void) { return s_started; }

static sp624e_group_api_result_t submit_group_api(group_api_request_t *request,
                                                  uint32_t *accepted_generation)
{
    if (!s_started || request == NULL || s_group_api_queue == NULL) {
        return SP624E_GROUP_API_NOT_READY;
    }
    if (xSemaphoreTake(s_group_api_call_lock,
                       pdMS_TO_TICKS(GROUP_API_LOCK_TIMEOUT_MS)) != pdTRUE) {
        runtime_health_increment(&s_group_api_busy);
        return SP624E_GROUP_API_BUSY;
    }
    request->id = ++s_group_api_next_id;
    if (request->id == 0) request->id = ++s_group_api_next_id;
    group_api_response_t response;
    while (xQueueReceive(s_group_api_response_queue, &response, 0) == pdTRUE) {}
    if (xQueueSend(s_group_api_queue, request, 0) != pdTRUE) {
        runtime_health_increment(&s_group_api_busy);
        xSemaphoreGive(s_group_api_call_lock);
        return SP624E_GROUP_API_BUSY;
    }
    int64_t deadline = now_ms() + GROUP_API_RESPONSE_TIMEOUT_MS;
    bool matched = false;
    while (now_ms() < deadline) {
        TickType_t remaining = pdMS_TO_TICKS(deadline - now_ms());
        if (remaining == 0) remaining = 1;
        if (xQueueReceive(s_group_api_response_queue, &response, remaining) != pdTRUE) break;
        if (response.id == request->id) {
            matched = true;
            break;
        }
        ESP_LOGW(TAG, "Discarded stale Group API response id=%" PRIu32
                      " expected=%" PRIu32, response.id, request->id);
    }
    if (!matched) {
        runtime_health_increment(&s_group_api_timeouts);
        xSemaphoreGive(s_group_api_call_lock);
        return SP624E_GROUP_API_TIMEOUT;
    }
    if (accepted_generation != NULL) *accepted_generation = response.generation;
    sp624e_group_api_result_t result = response.result;
    xSemaphoreGive(s_group_api_call_lock);
    return result;
}

void sp624e_controller_get_runtime_health(sp624e_runtime_health_t *health)
{
    if (health == NULL) return;
    portENTER_CRITICAL(&s_runtime_health_mux);
    *health = (sp624e_runtime_health_t) {
        .heartbeat_ms = s_runtime_heartbeat_ms,
        .api_timeouts = s_group_api_timeouts,
        .api_busy = s_group_api_busy,
        .api_response_drops = s_group_api_response_drops,
    };
    portEXIT_CRITICAL(&s_runtime_health_mux);
}

sp624e_group_api_result_t sp624e_group_set_rgb(uint8_t red, uint8_t green,
                                               uint8_t blue, uint8_t brightness,
                                               uint32_t *accepted_generation)
{
    if (!animation_manager_cancel_for_user_and_wait(
            APP_ANIMATION_RESTORE_TIMEOUT_MS + ANIMATION_CANCEL_WAIT_GRACE_MS)) {
        return SP624E_GROUP_API_TIMEOUT;
    }
    group_api_request_t request = {
        .type = GROUP_API_SET_RGB, .red = red, .green = green,
        .blue = blue, .level = brightness,
    };
    return submit_group_api(&request, accepted_generation);
}

sp624e_group_api_result_t sp624e_group_set_white(uint8_t level,
                                                 uint32_t *accepted_generation)
{
    if (!animation_manager_cancel_for_user_and_wait(
            APP_ANIMATION_RESTORE_TIMEOUT_MS + ANIMATION_CANCEL_WAIT_GRACE_MS)) {
        return SP624E_GROUP_API_TIMEOUT;
    }
    group_api_request_t request = {.type = GROUP_API_SET_WHITE, .level = level};
    return submit_group_api(&request, accepted_generation);
}

sp624e_group_api_result_t sp624e_group_force_resync(uint32_t *accepted_generation)
{
    if (!animation_manager_cancel_for_user_and_wait(
            APP_ANIMATION_RESTORE_TIMEOUT_MS + ANIMATION_CANCEL_WAIT_GRACE_MS)) {
        return SP624E_GROUP_API_TIMEOUT;
    }
    group_api_request_t request = {.type = GROUP_API_RESYNC};
    return submit_group_api(&request, accepted_generation);
}

sp624e_group_api_result_t sp624e_group_apply_favorite(uint32_t *accepted_generation)
{
    sp624e_favorite_preset_t preset;
    preset_manager_get_favorite(&preset);
    return sp624e_group_set_rgb(preset.red, preset.green, preset.blue,
                                preset.brightness, accepted_generation);
}

void sp624e_group_get_snapshot(sp624e_group_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->controller_started = s_started;
    if (!s_started || s_lock == NULL) return;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        ble_connection_manager_get_status((sp624e_side_t)i,
                                          &snapshot->sides[i].connection);
        sp624e_metrics_get_side((sp624e_side_t)i, &snapshot->sides[i].metrics);
    }
    lock();
    snapshot->white_available = s_white_available;
    snapshot->group_state = s_group_state;
    snapshot->desired = s_desired;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        snapshot->sides[i].observed = s_sides[i].observed;
        snapshot->sides[i].applied_generation = s_sides[i].last_applied_generation;
        snapshot->sides[i].verified_generation = s_sides[i].last_verified_generation;
        snapshot->animation_frames_sent[i] = s_animation_frames_sent[i];
    }
    snapshot->animation_coalesced_commands =
        (sp624e_command_queue_coalesced_count(&s_sides[0].queue) -
         s_animation_coalesced_base[0]) +
        (sp624e_command_queue_coalesced_count(&s_sides[1].queue) -
         s_animation_coalesced_base[1]);
    snapshot->animation_max_queue_depth = s_animation_max_queue_depth;
    unlock();
}

void sp624e_controller_on_notification(uint16_t conn_handle, uint16_t attr_handle,
                                       const uint8_t *data, size_t length)
{
    if (!s_started || data == NULL || length == 0 ||
        length > sizeof(((notify_packet_t *)0)->data)) return;
    notify_packet_t packet = {
        .conn_handle = conn_handle,
        .attr_handle = attr_handle,
        .length = (uint8_t)length,
    };
    memcpy(packet.data, data, length);
    if (xQueueSend(s_notify_queue, &packet, 0) != pdTRUE) {
        ESP_EARLY_LOGW(TAG_NOTIFY, "notification queue full");
    }
}

void sp624e_controller_on_disconnect(const ble_addr_t *address, int reason)
{
    if (!s_started) return;
    side_context_t *side = side_by_address(address);
    if (side == NULL) return;
    lock();
    bool was_controller_connected = side->connected;
    side->connected = false;
    /* Preserve the generation from the first real controller drop. Failed
     * GAP/GATT recovery attempts must not rewrite it to a newer Desired State,
     * or the recovered side could reconcile before the strict group dispatch. */
    if (was_controller_connected) {
        side->generation_at_disconnect = s_desired.generation;
    }
    side->last_verified_generation = 0;
    unlock();
    animation_manager_on_disconnect();
    side->notifications_enabled = false;
    side->initial_query_done = false;
    side->write_busy = false;
    side->reconcile_queued = false;
    sp624e_reassembly_reset(&side->reassembly);
    sp624e_command_queue_clear(&side->queue);
    xSemaphoreGive(side->state_done);
    xSemaphoreGive(side->write_done);
    set_group_state(SP624E_GROUP_DEGRADED, "one side disconnected; healthy side retained");
    ESP_LOGW(TAG, "%s disconnect reason=0x%X desired_generation=%" PRIu32,
             sp624e_side_name(side->side), reason, side->generation_at_disconnect);
}

void sp624e_controller_on_fast_recovery(sp624e_side_t side)
{
    if (!s_started || side >= SP624E_SIDE_COUNT) return;
    set_group_state(SP624E_GROUP_POWER_CYCLE_RECOVERY,
                    "power-cycle compatible BLE loss; recovering connection");
}

static bool enqueue_animation_wire(side_context_t *side, sp624e_command_type_t type,
                                   uint32_t session_id, const uint8_t *payload,
                                   size_t payload_len, bool allow_coalescing)
{
    sp624e_command_t command = {
        .generation = 0, .session_id = session_id, .type = type,
        .payload_len = payload_len, .requires_verification = false,
    };
    memcpy(command.payload, payload, payload_len);
    bool accepted = sp624e_command_queue_push(&side->queue, &command,
                                               allow_coalescing);
    if (accepted) {
        sp624e_connection_metrics_t metrics;
        sp624e_metrics_get_side(side->side, &metrics);
        metrics.queue_depth = (uint32_t)sp624e_command_queue_depth(&side->queue);
        if (metrics.queue_depth > metrics.max_queue_depth) metrics.max_queue_depth = metrics.queue_depth;
        metrics.coalesced_commands = sp624e_command_queue_coalesced_count(&side->queue);
        sp624e_metrics_set_side(side->side, &metrics);
    }
    return accepted;
}

bool sp624e_group_animation_begin(uint32_t animation_generation)
{
    if (!s_started || animation_generation == 0 ||
        !ble_connection_manager_both_ready()) return false;
    lock();
    bool ready = s_group_state == SP624E_GROUP_SYNCED &&
                 !s_animation_active && s_desired.valid;
    if (ready) {
        s_animation_active = true;
        s_animation_generation = animation_generation;
        s_animation_mode = -1;
        memset(s_animation_frames_sent, 0, sizeof(s_animation_frames_sent));
        s_animation_coalesced_base[0] =
            sp624e_command_queue_coalesced_count(&s_sides[0].queue);
        s_animation_coalesced_base[1] =
            sp624e_command_queue_coalesced_count(&s_sides[1].queue);
        s_animation_max_queue_depth = 0;
    }
    unlock();
    return ready;
}

bool sp624e_group_animation_frame_ready(uint32_t animation_generation)
{
    if (!s_started || animation_generation == 0 ||
        !ble_connection_manager_both_ready()) return false;
    lock();
    bool active = s_animation_active &&
                  s_animation_generation == animation_generation;
    unlock();
    if (!active) return false;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        if (sp624e_command_queue_depth(&s_sides[i].queue) > 0) return false;
    }
    return true;
}

bool sp624e_group_animation_frame(uint32_t animation_generation,
                                  const animation_frame_t *frame)
{
    if (frame == NULL || !ble_connection_manager_both_ready()) return false;
    lock();
    bool active = s_animation_active && s_animation_generation == animation_generation;
    int prior_mode = s_animation_mode;
    unlock();
    if (!active) return false;

    if (frame->transition_barrier) {
        for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
            if (sp624e_command_queue_depth(&s_sides[i].queue) > 0) return false;
        }
    }

    uint8_t effect[SP624E_COMMAND_MAX_LEN];
    uint8_t value[SP624E_COMMAND_MAX_LEN];
    size_t effect_len = 0, value_len = 0;
    sp624e_protocol_result_t result;
    if (prior_mode != (int)frame->mode) {
        result = sp624e_build_effect(frame->mode == ANIMATION_MODE_WHITE ?
                                     SP624E_EFFECT_WHITE : SP624E_EFFECT_SOLID,
                                     effect, sizeof(effect), &effect_len);
        if (result != SP624E_PROTOCOL_OK) return false;
    }
    sp624e_command_type_t type;
    if (frame->mode == ANIMATION_MODE_WHITE) {
        result = sp624e_build_white(frame->brightness, value, sizeof(value), &value_len);
        type = SP624E_COMMAND_BRIGHTNESS;
    } else {
        result = sp624e_build_rgb(frame->red, frame->green, frame->blue,
                                  frame->brightness, value, sizeof(value), &value_len);
        type = SP624E_COMMAND_RGB;
    }
    if (result != SP624E_PROTOCOL_OK) return false;

    size_t required_slots = effect_len > 0 ? 2u : 1u;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        size_t depth = sp624e_command_queue_depth(&s_sides[i].queue);
        if (depth > SP624E_COMMAND_QUEUE_CAPACITY - required_slots) return false;
    }

    bool accepted[SP624E_SIDE_COUNT] = {true, true};
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        if (effect_len > 0) {
            accepted[i] = enqueue_animation_wire(&s_sides[i], SP624E_COMMAND_EFFECT,
                                                  animation_generation, effect, effect_len,
                                                  !frame->transition_barrier);
        }
        if (accepted[i]) {
            accepted[i] = enqueue_animation_wire(&s_sides[i], type,
                                                  animation_generation, value, value_len,
                                                  !frame->transition_barrier);
        }
    }
    if (!accepted[0] || !accepted[1]) {
        for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
            sp624e_command_queue_clear(&s_sides[i].queue);
        }
        return false;
    }
    lock();
    if (s_animation_active && s_animation_generation == animation_generation) {
        s_animation_mode = (int)frame->mode;
    } else {
        accepted[0] = accepted[1] = false;
    }
    unlock();
    if (!accepted[0] || !accepted[1]) {
        for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
            sp624e_command_queue_clear(&s_sides[i].queue);
        }
        return false;
    }
    /* Wake both workers only after both queues contain matching session data. */
    xTaskNotifyGive(s_sides[SP624E_SIDE_LEFT].worker_task);
    xTaskNotifyGive(s_sides[SP624E_SIDE_RIGHT].worker_task);
    return true;
}

void sp624e_group_animation_end(uint32_t animation_generation)
{
    if (!s_started) return;
    lock();
    if (!s_animation_active || s_animation_generation != animation_generation) {
        unlock();
        return;
    }
    s_animation_active = false;
    s_animation_generation++;
    s_animation_mode = -1;
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        s_sides[i].last_verified_generation = 0;
        s_sides[i].reconcile_queued = false;
        s_sides[i].force_desired_write = true;
    }
    unlock();
    for (size_t i = 0; i < SP624E_SIDE_COUNT; ++i) {
        sp624e_command_queue_clear(&s_sides[i].queue);
    }
    set_group_state(SP624E_GROUP_RECONCILING,
                    "animation ended; restoring persistent Desired State");
    maybe_schedule_group_reconcile();
}

void sp624e_controller_on_recovered(sp624e_side_t side_index,
                                    const sp624e_transport_t *transport)
{
    if (!s_started || transport == NULL || side_index >= SP624E_SIDE_COUNT) return;
    side_context_t *side = &s_sides[side_index];
    side->transport = *transport;
    lock();
    side->connected = true;
    unlock();
    side->notifications_enabled = false;
    side->initial_query_done = false;
    side->write_busy = false;
    sp624e_reassembly_reset(&side->reassembly);
    enqueue(side, SP624E_COMMAND_ENABLE_NOTIFICATIONS, 0, false);
}
