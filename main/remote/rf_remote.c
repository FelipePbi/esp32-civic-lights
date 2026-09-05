#include "rf_remote.h"

#include <inttypes.h>
#include <string.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rf_input.h"

static const char *TAG = "RF";
static rf_remote_snapshot_t s_snapshot;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static rf_remote_event_fn s_event_fn;

static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static uint8_t sample_channels(void)
{
    uint8_t mask = 0;
    if (gpio_get_level(APP_RF_D0_GPIO)) mask |= 1u << RF_CHANNEL_D0;
    if (gpio_get_level(APP_RF_D1_GPIO)) mask |= 1u << RF_CHANNEL_D1;
    if (gpio_get_level(APP_RF_D2_GPIO)) mask |= 1u << RF_CHANNEL_D2;
    if (gpio_get_level(APP_RF_D3_GPIO)) mask |= 1u << RF_CHANNEL_D3;
    return mask;
}

static void input_task(void *arg)
{
    (void)arg;
    bool watched = esp_task_wdt_add(NULL) == ESP_OK;
    uint8_t initial = sample_channels();
    rf_input_filter_t filter;
    rf_input_filter_init(&filter, initial, now_ms());
    while (true) {
        uint8_t mask = sample_channels();
        bool vt = gpio_get_level(APP_RF_VT_GPIO) != 0;
        lock(); s_snapshot.vt_active = vt; unlock();
        rf_input_event_t event;
        if (rf_input_filter_sample(&filter, mask, vt, now_ms(),
                                   APP_RF_DEBOUNCE_MS, &event)) {
            rf_remote_config_t config;
            rf_config_get(&config);
            uint64_t timestamp = now_ms();
            lock();
            bool discovery = s_snapshot.discovery_active;
            s_snapshot.has_last_channel = true;
            s_snapshot.last_channel = event.channel;
            s_snapshot.last_event_ms = timestamp;
            s_snapshot.mapping_complete =
                rf_config_mapping_valid(config.channel_map, true);
            unlock();
            remote_button_t button = rf_config_resolve_button(
                &config, event.channel, discovery);
            remote_button_t configured_button =
                config.channel_map[event.channel];
            ESP_LOGI(TAG, "RF INPUT VT=%d D0=%d D1=%d D2=%d D3=%d",
                     event.vt_active, (mask & 1u) != 0, (mask & 2u) != 0,
                     (mask & 4u) != 0, (mask & 8u) != 0);
            ESP_LOGI(TAG, "physical channel %s mapped to %s",
                     rf_physical_channel_name(event.channel),
                     remote_button_name(configured_button));
            if (discovery) {
                ESP_LOGI(TAG, "discovery active: logical action suppressed");
                if (s_event_fn != NULL) {
                    s_event_fn(REMOTE_BUTTON_INVALID, event.channel, event.vt_active);
                }
            } else if (button == REMOTE_BUTTON_INVALID) {
                ESP_LOGW(TAG, "discovery only: persist physical button mapping before actions");
                if (s_event_fn != NULL) {
                    s_event_fn(REMOTE_BUTTON_INVALID, event.channel, event.vt_active);
                }
            } else if (s_event_fn != NULL) {
                s_event_fn(button, event.channel, event.vt_active);
            }
        }
        lock(); s_snapshot.heartbeat_ms = (int64_t)now_ms(); unlock();
        if (watched) esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(APP_RF_POLL_INTERVAL_MS));
    }
}

esp_err_t rf_remote_init(rf_remote_event_fn event_fn)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_event_fn = event_fn;
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << APP_RF_D0_GPIO) | (1ULL << APP_RF_D1_GPIO) |
                        (1ULL << APP_RF_D2_GPIO) | (1ULL << APP_RF_D3_GPIO) |
                        (1ULL << APP_RF_VT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) return err;
    rf_remote_config_t stored;
    rf_config_get(&stored);
    lock();
    s_snapshot.initialized = true;
    s_snapshot.mapping_complete = rf_config_mapping_valid(stored.channel_map, true);
    unlock();
    if (xTaskCreate(input_task, "rf_input", 4096, NULL, 4, NULL) != pdPASS) {
        lock(); s_snapshot.initialized = false; unlock();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "RX480E ready D0=%d D1=%d D2=%d D3=%d VT=%d mapping=%s",
             APP_RF_D0_GPIO, APP_RF_D1_GPIO, APP_RF_D2_GPIO, APP_RF_D3_GPIO,
             APP_RF_VT_GPIO, s_snapshot.mapping_complete ? "READY" : "DISCOVERY");
    return ESP_OK;
}

void rf_remote_get_snapshot(rf_remote_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_lock == NULL) return;
    lock(); *snapshot = s_snapshot; unlock();
    rf_remote_config_t config;
    rf_config_get(&config);
    snapshot->mapping_complete = rf_config_mapping_valid(config.channel_map, true);
}

bool rf_remote_set_discovery(bool enabled)
{
    if (s_lock == NULL) return false;
    lock();
    if (!s_snapshot.initialized) {
        unlock();
        return false;
    }
    s_snapshot.discovery_active = enabled;
    unlock();
    ESP_LOGI(TAG, "discovery %s; logical actions %s", enabled ? "ON" : "OFF",
             enabled ? "SUPPRESSED" : "ENABLED");
    return true;
}
