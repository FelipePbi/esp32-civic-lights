#include "desired_state.h"

#include <string.h>

#include "nvs.h"

#define NVS_NAMESPACE "sp624e"
#define NVS_DESIRED_KEY "desired"
#define NVS_DESIRED_VERSION_KEY "desired_ver"
#define NVS_RESTORE_KEY "restore_boot"

typedef struct {
    bool valid;
    uint32_t generation;
    bool power;
    uint8_t effect;
    uint8_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    uint8_t white;
    uint8_t speed;
} sp624e_desired_state_v1_t;

esp_err_t sp624e_desired_load(sp624e_desired_state_t *desired, bool *restore_on_boot)
{
    if (desired == NULL || restore_on_boot == NULL) return ESP_ERR_INVALID_ARG;
    memset(desired, 0, sizeof(*desired));
    *restore_on_boot = false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint32_t version = 0;
    err = nvs_get_u32(handle, NVS_DESIRED_VERSION_KEY, &version);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK || (version != 1 && version != SP624E_DESIRED_STATE_VERSION)) {
        nvs_close(handle);
        return err == ESP_OK ? ESP_ERR_INVALID_VERSION : err;
    }
    size_t length = version == 1 ? sizeof(sp624e_desired_state_v1_t) : sizeof(*desired);
    if (version == 1) {
        sp624e_desired_state_v1_t legacy;
        err = nvs_get_blob(handle, NVS_DESIRED_KEY, &legacy, &length);
        if (err == ESP_OK && length == sizeof(legacy)) {
            desired->valid = legacy.valid;
            desired->generation = legacy.generation;
            desired->power = legacy.power;
            desired->light_mode = legacy.effect == SP624E_EFFECT_WHITE ?
                                  SP624E_LIGHT_MODE_WHITE : SP624E_LIGHT_MODE_RGB;
            desired->effect = legacy.effect;
            desired->mode = legacy.mode;
            desired->red = legacy.red;
            desired->green = legacy.green;
            desired->blue = legacy.blue;
            desired->brightness = legacy.brightness;
            desired->white = legacy.white;
            desired->speed = legacy.speed;
        }
    } else {
        err = nvs_get_blob(handle, NVS_DESIRED_KEY, desired, &length);
    }
    uint8_t restore = 0;
    esp_err_t restore_err = nvs_get_u8(handle, NVS_RESTORE_KEY, &restore);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    size_t expected = version == 1 ? sizeof(sp624e_desired_state_v1_t) : sizeof(*desired);
    if (length != expected || !desired->valid || desired->generation == 0 ||
        desired->light_mode > SP624E_LIGHT_MODE_WHITE) {
        memset(desired, 0, sizeof(*desired));
        return ESP_ERR_INVALID_STATE;
    }
    if (restore_err != ESP_OK && restore_err != ESP_ERR_NVS_NOT_FOUND) return restore_err;
    *restore_on_boot = restore_err == ESP_OK && restore != 0;
    return ESP_OK;
}

esp_err_t sp624e_desired_save(const sp624e_desired_state_t *desired,
                              bool restore_on_boot)
{
    if (desired == NULL || !desired->valid || desired->generation == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, NVS_DESIRED_VERSION_KEY, SP624E_DESIRED_STATE_VERSION);
    if (err == ESP_OK) err = nvs_set_blob(handle, NVS_DESIRED_KEY, desired, sizeof(*desired));
    if (err == ESP_OK) err = nvs_set_u8(handle, NVS_RESTORE_KEY, restore_on_boot ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
