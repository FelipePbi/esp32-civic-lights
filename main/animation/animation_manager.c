#include "animation_manager.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void animation_manager_init(runtime_animation_event_fn event_fn)
{
    runtime_animation_init(event_fn);
}

void animation_manager_cancel_for_user(void)
{
    runtime_animation_cancel_for_user();
}

bool animation_manager_cancel_for_user_and_wait(uint32_t timeout_ms)
{
    bool had_runtime = runtime_animation_is_active();
    animation_manager_cancel_for_user();
    if (!had_runtime && !animation_manager_any_active()) return true;

    int64_t deadline = now_ms() + timeout_ms;
    while (animation_manager_any_active() && now_ms() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (animation_manager_any_active()) return false;

    runtime_animation_snapshot_t runtime = {0};
    runtime_animation_get_snapshot(&runtime);
    return !had_runtime || runtime.state != RUNTIME_ANIMATION_FAILED;
}

void animation_manager_on_disconnect(void)
{
    runtime_animation_on_disconnect();
}

bool animation_manager_any_active(void)
{
    return runtime_animation_is_active();
}
