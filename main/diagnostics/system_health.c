#include "system_health.h"
#include "system_health_policy.h"

#include <string.h>
#include <stdio.h>

#include "ble/ble_connection_manager.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "indicator/indicator.h"
#include "diagnostics/connection_metrics.h"
#include "remote/remote_controller.h"
#include "remote/rf_remote.h"
#include "sp624e/sp624e_controller.h"
#include "web/websocket.h"

#define HEALTH_RTC_MAGIC 0x4845414cU
#define HEALTH_STARTUP_GRACE_MS 25000
#define HEALTH_STALE_MS 2000
#define HEALTH_RESTART_MS 8000

typedef struct {
    uint32_t magic;
    uint32_t restart_count;
    char reason[32];
} rtc_health_record_t;

RTC_NOINIT_ATTR static rtc_health_record_t s_rtc_record;
static const char *TAG = "SYSTEM_HEALTH";
static system_health_snapshot_t s_snapshot;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static int64_t s_started_ms;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "POWER_ON";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    default: return "OTHER";
    }
}

static bool heartbeat_stale(int64_t heartbeat, int64_t now)
{
    return system_health_heartbeat_stale(heartbeat, now, HEALTH_STALE_MS);
}

static void restart_for(const char *component)
{
    s_rtc_record.magic = HEALTH_RTC_MAGIC;
    s_rtc_record.restart_count++;
    snprintf(s_rtc_record.reason, sizeof(s_rtc_record.reason), "%s_stalled", component);
    ESP_LOGE(TAG, "Restarting: %s stale for %dms", component, HEALTH_RESTART_MS);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

static void supervisor_task(void *arg)
{
    (void)arg;
    bool watched = esp_task_wdt_add(NULL) == ESP_OK;
    int64_t stale_since = 0;
    char stale_component[24] = {0};
    while (true) {
        int64_t now = now_ms();
        sp624e_runtime_health_t group = {0};
        rf_remote_snapshot_t rf = {0};
        remote_controller_snapshot_t remote = {0};
        indicator_snapshot_t indicator = {0};
        sp624e_controller_get_runtime_health(&group);
        rf_remote_get_snapshot(&rf);
        remote_controller_get_snapshot(&remote);
        indicator_get_snapshot(&indicator);
        int64_t manager_hb = ble_connection_manager_heartbeat_ms();
        int64_t web_hb = websocket_heartbeat_ms();
        const char *stale = NULL;
        if (now - s_started_ms > HEALTH_STARTUP_GRACE_MS) {
            if (heartbeat_stale(manager_hb, now)) stale = "connection_mgr";
            else if (sp624e_controller_is_started() &&
                     heartbeat_stale(group.heartbeat_ms, now)) stale = "group_runtime";
            else if (heartbeat_stale((int64_t)rf.heartbeat_ms, now)) stale = "rf_input";
            else if (heartbeat_stale(indicator.heartbeat_ms, now)) stale = "indicator";
            else if (heartbeat_stale(web_hb, now)) stale = "web_events";
        }
        if (stale == NULL) {
            stale_since = 0;
            stale_component[0] = '\0';
        } else if (strcmp(stale_component, stale) != 0) {
            snprintf(stale_component, sizeof(stale_component), "%s", stale);
            stale_since = now;
            ESP_LOGE(TAG, "Critical heartbeat stale: %s", stale);
        } else if (system_health_restart_due(stale_since, now,
                                              HEALTH_RESTART_MS)) {
            restart_for(stale_component);
        }
        sp624e_connection_metrics_t left = {0};
        sp624e_connection_metrics_t right = {0};
        sp624e_metrics_get_side(SP624E_SIDE_LEFT, &left);
        sp624e_metrics_get_side(SP624E_SIDE_RIGHT, &right);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_snapshot.healthy = stale == NULL;
        snprintf(s_snapshot.stale_component, sizeof(s_snapshot.stale_component),
                 "%s", stale != NULL ? stale : "");
        s_snapshot.free_heap = esp_get_free_heap_size();
        s_snapshot.minimum_free_heap = esp_get_minimum_free_heap_size();
        s_snapshot.connection_manager_heartbeat_ms = manager_hb;
        s_snapshot.group_runtime_heartbeat_ms = group.heartbeat_ms;
        s_snapshot.rf_heartbeat_ms = (int64_t)rf.heartbeat_ms;
        s_snapshot.indicator_heartbeat_ms = indicator.heartbeat_ms;
        s_snapshot.web_heartbeat_ms = web_hb;
        s_snapshot.ble_forced_recoveries = left.forced_recoveries + right.forced_recoveries;
        s_snapshot.ble_critical_event_replacements =
            left.critical_event_replacements + right.critical_event_replacements;
        s_snapshot.group_api_timeouts = group.api_timeouts;
        s_snapshot.group_api_busy = group.api_busy;
        s_snapshot.group_api_response_drops = group.api_response_drops;
        s_snapshot.rf_event_drops = remote.event_drops;
        s_snapshot.websocket_event_drops = websocket_event_drop_count();
        s_snapshot.indicator_gpio_level = indicator.gpio_level;
        s_snapshot.indicator_last_change_ms = indicator.last_change_ms;
        xSemaphoreGive(s_lock);
        if (watched) esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t system_health_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    esp_reset_reason_t reason = esp_reset_reason();
    s_snapshot.reset_reason = reason;
    snprintf(s_snapshot.reset_reason_name, sizeof(s_snapshot.reset_reason_name), "%s",
             reset_reason_name(reason));
    if (reason == ESP_RST_POWERON || s_rtc_record.magic != HEALTH_RTC_MAGIC) {
        memset(&s_rtc_record, 0, sizeof(s_rtc_record));
        s_rtc_record.magic = HEALTH_RTC_MAGIC;
    } else {
        snprintf(s_snapshot.previous_recovery_reason,
                 sizeof(s_snapshot.previous_recovery_reason), "%s", s_rtc_record.reason);
    }
    s_snapshot.supervisor_restart_count = s_rtc_record.restart_count;
    s_rtc_record.reason[0] = '\0';
    s_snapshot.healthy = true;
    s_started_ms = now_ms();
    ESP_LOGI(TAG, "Boot reset=%s previous_recovery=%s count=%u",
             s_snapshot.reset_reason_name,
             s_snapshot.previous_recovery_reason[0] != '\0' ?
                 s_snapshot.previous_recovery_reason : "none",
             s_snapshot.supervisor_restart_count);
    if (xTaskCreate(supervisor_task, "system_health", 4096, NULL, 8, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void system_health_get_snapshot(system_health_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *snapshot = s_snapshot;
    xSemaphoreGive(s_lock);
}
