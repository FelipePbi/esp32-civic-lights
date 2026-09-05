#include "preset_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_log.h"

#define NVS_NAMESPACE "web_preset"
#define NVS_FAVORITE_KEY "favorite"
#define NVS_WHITE_KEY "white_ok"

static sp624e_favorite_preset_t s_favorite;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static bool s_white_available;
static const char *TAG = "PRESET";

static sp624e_favorite_preset_t default_favorite(void)
{
    return (sp624e_favorite_preset_t) {
        .version = SP624E_FAVORITE_VERSION,
        .red = 255,
        .green = 0,
        .blue = 0,
        .brightness = 64,
    };
}

esp_err_t preset_manager_init(void)
{
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    s_favorite = default_favorite();
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    sp624e_favorite_preset_t stored;
    size_t length = sizeof(stored);
    err = nvs_get_blob(handle, NVS_FAVORITE_KEY, &stored, &length);
    uint8_t white = 0;
    esp_err_t white_err = nvs_get_u8(handle, NVS_WHITE_KEY, &white);
    nvs_close(handle);
    if (white_err == ESP_OK) s_white_available = white != 0;
    else if (white_err != ESP_ERR_NVS_NOT_FOUND) return white_err;
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (length != sizeof(stored) || stored.version != SP624E_FAVORITE_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_favorite = stored;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool preset_manager_white_available(void)
{
    if (s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool available = s_white_available;
    xSemaphoreGive(s_lock);
    return available;
}

esp_err_t preset_manager_set_white_available(bool available)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, NVS_WHITE_KEY, available ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_white_available = available;
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "white capability persisted enabled=%d", available);
    }
    return err;
}

void preset_manager_get_favorite(sp624e_favorite_preset_t *preset)
{
    if (preset == NULL) return;
    if (s_lock == NULL) {
        *preset = default_favorite();
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *preset = s_favorite;
    xSemaphoreGive(s_lock);
}

esp_err_t preset_manager_save_favorite(const sp624e_favorite_preset_t *preset)
{
    if (preset == NULL || preset->version != SP624E_FAVORITE_VERSION) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, NVS_FAVORITE_KEY, preset, sizeof(*preset));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_favorite = *preset;
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "favorite persisted rgb=%u,%u,%u brightness=%u",
                 preset->red, preset->green, preset->blue, preset->brightness);
    }
    return err;
}
