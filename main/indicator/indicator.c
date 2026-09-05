#include "indicator.h"

#include <string.h>

#include "animation/animation_manager.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sp624e/sp624e_controller.h"

static const char *TAG = "INDICATOR";
static indicator_snapshot_t s_snapshot;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static bool s_test_active;

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static int gpio_level_for(bool on)
{
    return on ? APP_INDICATOR_ACTIVE_LEVEL : !APP_INDICATOR_ACTIVE_LEVEL;
}

static void indicator_task(void *arg)
{
    (void)arg;
    bool watched = esp_task_wdt_add(NULL) == ESP_OK;
    while (true) {
        lock(); bool test_active = s_test_active; unlock();
        lock(); s_snapshot.heartbeat_ms = esp_timer_get_time() / 1000; unlock();
        if (watched) esp_task_wdt_reset();
        if (test_active) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        sp624e_group_snapshot_t group;
        sp624e_group_get_snapshot(&group);
        bool physical_group_ready =
            group.controller_started &&
            group.sides[SP624E_SIDE_LEFT].connection.state == BLE_CONNECTION_READY &&
            group.sides[SP624E_SIDE_RIGHT].connection.state == BLE_CONNECTION_READY;
        lock(); bool previous = s_snapshot.on; unlock();
        indicator_decision_t decision = indicator_policy_evaluate(
            previous, animation_manager_any_active(),
            physical_group_ready, group.group_state,
            &group.desired, &group.sides[0].observed, &group.sides[1].observed);
        int gpio_level = gpio_level_for(decision.on);
        lock();
        bool changed = decision.on != s_snapshot.on || decision.reason != s_snapshot.reason;
        s_snapshot.on = decision.on;
        s_snapshot.gpio_level = gpio_level;
        s_snapshot.reason = decision.reason;
        if (changed) s_snapshot.last_change_ms = esp_timer_get_time() / 1000;
        s_snapshot.heartbeat_ms = esp_timer_get_time() / 1000;
        unlock();
        gpio_set_level(APP_INDICATOR_GPIO, gpio_level);
        if (changed) {
            ESP_LOGI(TAG, "%s reason=%s gpio=%d", decision.on ? "ON" : "OFF",
                     indicator_reason_name(decision.reason), gpio_level);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void set_test_level(bool on)
{
    int gpio_level = gpio_level_for(on);
    gpio_set_level(APP_INDICATOR_GPIO, gpio_level);
    lock();
    s_snapshot.on = on;
    s_snapshot.gpio_level = gpio_level;
    s_snapshot.reason = INDICATOR_REASON_SELF_TEST;
    s_snapshot.last_change_ms = esp_timer_get_time() / 1000;
    s_snapshot.heartbeat_ms = s_snapshot.last_change_ms;
    unlock();
    ESP_LOGI(TAG, "%s reason=SELF_TEST gpio=%d", on ? "ON" : "OFF", gpio_level);
}

static void self_test_task(void *arg)
{
    (void)arg;
    set_test_level(false);
    vTaskDelay(pdMS_TO_TICKS(1000));
    set_test_level(true);
    vTaskDelay(pdMS_TO_TICKS(1000));
    set_test_level(false);
    lock(); s_test_active = false; unlock();
    vTaskDelete(NULL);
}

esp_err_t indicator_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.reason = INDICATOR_REASON_BOOT;
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << APP_INDICATOR_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) return err;
    int inactive_level = gpio_level_for(false);
    err = gpio_set_level(APP_INDICATOR_GPIO, inactive_level);
    if (err != ESP_OK) return err;
    lock(); s_snapshot.initialized = true; unlock();
    if (xTaskCreate(indicator_task, "indicator", 4096, NULL, 3, NULL) != pdPASS) {
        lock(); s_snapshot.initialized = false; unlock();
        return ESP_ERR_NO_MEM;
    }
    lock(); s_snapshot.gpio_level = inactive_level; unlock();
    ESP_LOGI(TAG, "GPIO%d initialized inactive gpio=%d active=%d",
             APP_INDICATOR_GPIO, inactive_level, APP_INDICATOR_ACTIVE_LEVEL);
    return ESP_OK;
}

void indicator_get_snapshot(indicator_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_lock == NULL) return;
    lock(); *snapshot = s_snapshot; unlock();
}

bool indicator_run_self_test(void)
{
    if (s_lock == NULL) return false;
    lock();
    if (s_test_active) {
        unlock();
        return false;
    }
    s_test_active = true;
    unlock();
    if (xTaskCreate(self_test_task, "indicator_test", 2048, NULL, 3, NULL) != pdPASS) {
        lock(); s_test_active = false; unlock();
        return false;
    }
    return true;
}
