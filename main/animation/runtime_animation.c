#include "runtime_animation.h"

#include <inttypes.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "police_animation.h"
#include "remote/rf_config.h"
#include "sp624e/sp624e_controller.h"

static const char *TAG = "RUNTIME_ANIM";
static runtime_animation_snapshot_t s_snapshot;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static runtime_animation_event_fn s_event_fn;
static bool s_start_requested;
static bool s_cancel_requested;
static int64_t s_request_ms;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

const char *runtime_animation_state_name(runtime_animation_state_t state)
{
    switch (state) {
    case RUNTIME_ANIMATION_IDLE: return "idle";
    case RUNTIME_ANIMATION_STARTING: return "starting";
    case RUNTIME_ANIMATION_RUNNING: return "running";
    case RUNTIME_ANIMATION_CANCELLING: return "cancelling";
    case RUNTIME_ANIMATION_FAILED: return "failed";
    default: return "failed";
    }
}

static void publish(const char *event)
{
    runtime_animation_event_fn callback;
    runtime_animation_snapshot_t snapshot;
    lock(); callback = s_event_fn; snapshot = s_snapshot; unlock();
    ESP_LOGI(TAG, "POLICE_EVENT type=%s generation=%" PRIu32
             " elapsed=%" PRIu32 " frames=%" PRIu32 "/%" PRIu32,
             event, snapshot.generation, snapshot.elapsed_ms,
             snapshot.accepted_frames, snapshot.generated_frames);
    if (callback != NULL) callback(event);
}

static bool group_ready(void)
{
    sp624e_group_snapshot_t group;
    sp624e_group_get_snapshot(&group);
    return group.controller_started && group.group_state == SP624E_GROUP_SYNCED &&
           group.sides[0].connection.state == BLE_CONNECTION_READY &&
           group.sides[1].connection.state == BLE_CONNECTION_READY;
}

static bool restore_verified(uint32_t generation)
{
    sp624e_group_animation_end(generation);
    int64_t deadline = now_ms() + APP_ANIMATION_RESTORE_TIMEOUT_MS;
    while (now_ms() < deadline) {
        sp624e_group_snapshot_t group;
        sp624e_group_get_snapshot(&group);
        if (group.group_state == SP624E_GROUP_SYNCED &&
            group.sides[0].verified_generation == group.desired.generation &&
            group.sides[1].verified_generation == group.desired.generation) return true;
        if (group.group_state == SP624E_GROUP_DEGRADED) return false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static void finish_police(uint32_t generation, bool timed_out, bool disconnected)
{
    lock();
    s_snapshot.state = RUNTIME_ANIMATION_CANCELLING;
    s_snapshot.timed_out = timed_out;
    unlock();
    bool restored = restore_verified(generation);
    lock();
    s_snapshot.state = disconnected || restored ? RUNTIME_ANIMATION_IDLE :
                                                  RUNTIME_ANIMATION_FAILED;
    s_cancel_requested = false;
    unlock();
    publish(timed_out ? "police_timeout" :
            (restored || disconnected ? "police_stopped" : "police_failed"));
}

static void play_police(void)
{
    lock();
    uint32_t generation = ++s_snapshot.generation;
    if (generation == 0) generation = ++s_snapshot.generation;
    s_snapshot.state = RUNTIME_ANIMATION_RUNNING;
    s_snapshot.elapsed_ms = 0;
    s_snapshot.generated_frames = 0;
    s_snapshot.accepted_frames = 0;
    s_snapshot.dropped_frames = 0;
    s_snapshot.timed_out = false;
    s_cancel_requested = false;
    unlock();
    if (!sp624e_group_animation_begin(generation)) {
        lock(); s_snapshot.state = RUNTIME_ANIMATION_FAILED; unlock();
        publish("police_failed");
        return;
    }
    publish("police_started");
    int64_t started = now_ms();
    bool disconnected = false;
    bool timed_out = false;
    rf_remote_config_t remote_config;
    rf_config_get(&remote_config);
    police_speed_t speed = remote_config.police_speed;
    ESP_LOGI(TAG, "Police pattern speed=%s cycle=%u ms brightness=%u",
             police_speed_name(speed), police_animation_cycle_ms(speed),
             POLICE_PATTERN_BRIGHTNESS);
    uint8_t next_phase = 0;
    int64_t next_phase_due_ms = started;
    while (true) {
        uint32_t elapsed = (uint32_t)(now_ms() - started);
        lock();
        bool cancelled = s_cancel_requested;
        s_snapshot.elapsed_ms = elapsed;
        unlock();
        timed_out = elapsed >= APP_POLICE_TIMEOUT_MS;
        if (cancelled || timed_out) break;
        if (!group_ready()) { disconnected = true; break; }
        if (now_ms() >= next_phase_due_ms &&
            sp624e_group_animation_frame_ready(generation)) {
            animation_frame_t frame;
            uint8_t phase = next_phase;
            if (!police_animation_phase_frame(phase, &frame)) break;
            bool accepted = sp624e_group_animation_frame(generation, &frame);
            lock();
            s_snapshot.generated_frames++;
            if (accepted) s_snapshot.accepted_frames++;
            else s_snapshot.dropped_frames++;
            unlock();
            if (accepted) {
                next_phase = (uint8_t)((phase + 1u) % POLICE_PATTERN_PHASE_COUNT);
                next_phase_due_ms = now_ms() +
                    police_animation_phase_duration_ms(speed, phase);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    finish_police(generation, timed_out, disconnected);
}

static void runtime_task(void *arg)
{
    (void)arg;
    while (true) {
        bool start = false;
        bool start_failed = false;
        lock();
        bool requested = s_start_requested;
        bool cancelled = s_cancel_requested;
        int64_t requested_at = s_request_ms;
        unlock();
        bool ready = requested && !cancelled && group_ready();
        lock();
        if (s_start_requested && !s_cancel_requested && ready) {
            s_start_requested = false;
            start = true;
        } else if (s_start_requested && now_ms() - requested_at >
                                          APP_RUNTIME_ANIMATION_START_TIMEOUT_MS) {
            s_start_requested = false;
            s_snapshot.state = RUNTIME_ANIMATION_FAILED;
            start_failed = true;
        }
        unlock();
        if (start) play_police();
        else if (start_failed) publish("police_failed");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void runtime_animation_init(runtime_animation_event_fn event_fn)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    s_event_fn = event_fn;
    xTaskCreate(runtime_task, "runtime_animation", 6144, NULL, 4, NULL);
}

bool runtime_animation_start_police(void)
{
    lock();
    if (s_snapshot.state == RUNTIME_ANIMATION_RUNNING ||
        s_snapshot.state == RUNTIME_ANIMATION_STARTING || s_start_requested) {
        unlock();
        return false;
    }
    s_start_requested = true;
    s_cancel_requested = false;
    s_request_ms = now_ms();
    s_snapshot.state = RUNTIME_ANIMATION_STARTING;
    s_snapshot.timed_out = false;
    unlock();
    publish("police_starting");
    return true;
}

bool runtime_animation_toggle_police(void)
{
    if (runtime_animation_is_active()) return runtime_animation_stop();
    return runtime_animation_start_police();
}

bool runtime_animation_stop(void)
{
    lock();
    bool active = s_start_requested || s_snapshot.state == RUNTIME_ANIMATION_STARTING ||
                  s_snapshot.state == RUNTIME_ANIMATION_RUNNING;
    s_start_requested = false;
    if (active) s_cancel_requested = true;
    if (s_snapshot.state == RUNTIME_ANIMATION_STARTING) {
        s_snapshot.state = RUNTIME_ANIMATION_IDLE;
        s_cancel_requested = false;
    }
    unlock();
    return active;
}

void runtime_animation_cancel_for_user(void) { (void)runtime_animation_stop(); }
void runtime_animation_on_disconnect(void) { (void)runtime_animation_stop(); }

void runtime_animation_get_snapshot(runtime_animation_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_lock == NULL) return;
    lock(); *snapshot = s_snapshot; unlock();
}

bool runtime_animation_is_active(void)
{
    if (s_lock == NULL) return false;
    lock();
    bool active = s_start_requested || s_snapshot.state == RUNTIME_ANIMATION_STARTING ||
                  s_snapshot.state == RUNTIME_ANIMATION_RUNNING ||
                  s_snapshot.state == RUNTIME_ANIMATION_CANCELLING;
    unlock();
    return active;
}
