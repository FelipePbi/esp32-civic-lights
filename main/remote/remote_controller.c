#include "remote_controller.h"

#include <inttypes.h>
#include <string.h>

#include "animation/runtime_animation.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sp624e/sp624e_controller.h"
#include "remote_action.h"

#define REMOTE_EVENT_QUEUE_DEPTH 8
#define REMOTE_PENDING_TIMEOUT_MS 60000
#define REMOTE_RETRY_INTERVAL_MS 100

typedef struct {
    remote_button_t button;
    rf_physical_channel_t channel;
    bool vt_active;
    uint64_t timestamp_ms;
} remote_press_t;

static const char *TAG = "REMOTE_CTRL";
static remote_controller_snapshot_t s_snapshot;
static remote_controller_event_fn s_event_fn;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static StaticQueue_t s_queue_storage;
static uint8_t s_queue_buffer[REMOTE_EVENT_QUEUE_DEPTH * sizeof(remote_press_t)];
static QueueHandle_t s_queue;

static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static void publish(const char *event)
{
    remote_controller_event_fn callback = s_event_fn;
    if (callback != NULL) callback(event);
}

static void on_rf_event(remote_button_t button, rf_physical_channel_t channel,
                        bool vt_active)
{
    if (button == REMOTE_BUTTON_INVALID) {
        if (s_event_fn != NULL) s_event_fn("remote_button");
        return;
    }
    remote_press_t press = {
        .button = button,
        .channel = channel,
        .vt_active = vt_active,
        .timestamp_ms = now_ms(),
    };
    if (xQueueSend(s_queue, &press, 0) != pdTRUE) {
        lock(); s_snapshot.event_drops++; unlock();
        ESP_LOGW(TAG, "event queue full; dropped %s", remote_button_name(button));
    }
}

static bool apply_static_result(sp624e_group_api_result_t result)
{
    return result == SP624E_GROUP_API_OK;
}

static bool apply_plan(const remote_action_plan_t *plan)
{
    uint32_t generation = 0;
    switch (plan->type) {
    case REMOTE_INTENT_FAVORITE:
        return apply_static_result(sp624e_group_apply_favorite(&generation));
    case REMOTE_INTENT_RGB:
        return apply_static_result(sp624e_group_set_rgb(
            plan->red, plan->green, plan->blue, plan->brightness,
            &generation));
    case REMOTE_INTENT_WHITE:
        return apply_static_result(sp624e_group_set_white(
            plan->brightness, &generation));
    case REMOTE_INTENT_POLICE_TOGGLE:
        return runtime_animation_toggle_police();
    default:
        return false;
    }
}

static bool get_plan(remote_button_t button, remote_action_plan_t *plan)
{
    rf_remote_config_t config;
    rf_config_get(&config);
    return remote_action_plan(button, &config.button4, plan);
}

static bool plan_ready(const remote_action_plan_t *plan)
{
    sp624e_group_snapshot_t group;
    sp624e_group_get_snapshot(&group);
    bool both_ready =
        group.sides[SP624E_SIDE_LEFT].connection.state == BLE_CONNECTION_READY &&
        group.sides[SP624E_SIDE_RIGHT].connection.state == BLE_CONNECTION_READY;
    return remote_action_can_execute(
        plan, group.controller_started, both_ready,
        group.group_state == SP624E_GROUP_SYNCED, group.white_available);
}

static void controller_task(void *arg)
{
    (void)arg;
    remote_press_t press;
    remote_press_t pending_press = {0};
    remote_action_plan_t pending_plan = {0};
    bool pending = false;
    uint64_t pending_deadline_ms = 0;
    while (true) {
        TickType_t wait = pending ? pdMS_TO_TICKS(REMOTE_RETRY_INTERVAL_MS) :
                                    portMAX_DELAY;
        if (xQueueReceive(s_queue, &press, wait) == pdTRUE) {
            lock();
            s_snapshot.has_last_button = true;
            s_snapshot.last_button = press.button;
            s_snapshot.last_channel = press.channel;
            s_snapshot.last_event_ms = press.timestamp_ms;
            s_snapshot.last_action_accepted = false;
            unlock();
            ESP_LOGI(TAG, "%s from %s VT=%d", remote_button_name(press.button),
                     rf_physical_channel_name(press.channel), press.vt_active);
            publish("remote_button");
            if (!get_plan(press.button, &pending_plan)) {
                publish("remote_action_failed");
                pending = false;
                continue;
            }
            pending_press = press;
            pending_deadline_ms = now_ms() + REMOTE_PENDING_TIMEOUT_MS;
            pending = true;
            ESP_LOGI(TAG, "action pending intent=%d button=%s", pending_plan.type,
                     remote_button_name(pending_press.button));
            publish("remote_action_started");
        }
        if (!pending) continue;
        if (now_ms() >= pending_deadline_ms) {
            ESP_LOGW(TAG, "action expired button=%s; BLE group not ready",
                     remote_button_name(pending_press.button));
            lock(); s_snapshot.last_action_accepted = false; unlock();
            publish("remote_action_failed");
            pending = false;
            continue;
        }
        if (!plan_ready(&pending_plan)) continue;
        ESP_LOGI(TAG, "action executing intent=%d button=%s", pending_plan.type,
                 remote_button_name(pending_press.button));
        bool accepted = apply_plan(&pending_plan);
        if (!accepted) continue;
        lock(); s_snapshot.last_action_accepted = true; unlock();
        publish("remote_action_completed");
        pending = false;
    }
}

esp_err_t remote_controller_init(remote_controller_event_fn event_fn)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_event_fn = event_fn;
    esp_err_t config_err = rf_config_init();
    if (config_err != ESP_OK) {
        ESP_LOGW(TAG, "invalid persisted config ignored: %s",
                 esp_err_to_name(config_err));
    }
    s_queue = xQueueCreateStatic(REMOTE_EVENT_QUEUE_DEPTH, sizeof(remote_press_t),
                                 s_queue_buffer, &s_queue_storage);
    if (s_queue == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(controller_task, "remote_controller", 5120, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = rf_remote_init(on_rf_event);
    if (err != ESP_OK) return err;
    lock(); s_snapshot.initialized = true; unlock();
    ESP_LOGI(TAG, "remote controller initialized");
    return ESP_OK;
}

void remote_controller_get_snapshot(remote_controller_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_lock == NULL) return;
    lock(); *snapshot = s_snapshot; unlock();
    rf_remote_get_snapshot(&snapshot->receiver);
    rf_config_get(&snapshot->config);
}
