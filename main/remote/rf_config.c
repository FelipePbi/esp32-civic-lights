#include "rf_config.h"

#include <string.h>

#ifndef ESP_PLATFORM
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#endif

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#endif

#define NVS_NAMESPACE "rf_remote"
#define NVS_CONFIG_KEY "config"

static rf_remote_config_t s_config;

#ifdef ESP_PLATFORM
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
#else
static void lock(void) {}
static void unlock(void) {}
#endif

void rf_config_defaults(rf_remote_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->version = RF_REMOTE_CONFIG_VERSION;
    for (size_t i = 0; i < RF_CHANNEL_COUNT; ++i) {
        config->channel_map[i] = REMOTE_BUTTON_INVALID;
    }
    config->button4.type = REMOTE_ACTION_FAVORITE;
    config->button4.red = 255;
    config->button4.brightness = 64;
    config->police_speed = POLICE_SPEED_FAST;
}

bool rf_config_button4_valid(const remote_button4_config_t *config)
{
    if (config == NULL) return false;
    switch (config->type) {
    case REMOTE_ACTION_FAVORITE:
    case REMOTE_ACTION_RGB:
    case REMOTE_ACTION_WHITE:
    case REMOTE_ACTION_POLICE:
        return true;
    default:
        return false;
    }
}

bool rf_config_mapping_valid(const remote_button_t mapping[RF_CHANNEL_COUNT],
                             bool require_complete)
{
    if (mapping == NULL) return false;
    bool seen[REMOTE_BUTTON_COUNT] = {false};
    for (size_t i = 0; i < RF_CHANNEL_COUNT; ++i) {
        remote_button_t button = mapping[i];
        if (button == REMOTE_BUTTON_INVALID && !require_complete) continue;
        if (button < REMOTE_BUTTON_1 || button >= REMOTE_BUTTON_COUNT || seen[button]) {
            return false;
        }
        seen[button] = true;
    }
    if (!require_complete) return true;
    for (size_t i = 0; i < REMOTE_BUTTON_COUNT; ++i) {
        if (!seen[i]) return false;
    }
    return true;
}

bool rf_config_encode(const rf_remote_config_t *config,
                      uint8_t encoded[RF_REMOTE_CONFIG_ENCODED_SIZE])
{
    if (config == NULL || encoded == NULL ||
        config->version != RF_REMOTE_CONFIG_VERSION ||
        !rf_config_mapping_valid(config->channel_map, false) ||
        !rf_config_button4_valid(&config->button4) ||
        !police_speed_valid(config->police_speed)) return false;
    memset(encoded, 0, RF_REMOTE_CONFIG_ENCODED_SIZE);
    encoded[0] = RF_REMOTE_CONFIG_VERSION;
    for (size_t i = 0; i < RF_CHANNEL_COUNT; ++i) {
        encoded[1 + i] = (uint8_t)config->channel_map[i];
    }
    encoded[5] = (uint8_t)config->button4.type;
    encoded[6] = config->button4.red;
    encoded[7] = config->button4.green;
    encoded[8] = config->button4.blue;
    encoded[9] = config->button4.brightness;
    encoded[10] = 0; /* Reserved: kept for NVS v2 layout compatibility. */
    encoded[11] = (uint8_t)config->police_speed;
    encoded[12] = 0xa5;
    return true;
}

bool rf_config_decode(const uint8_t *encoded, size_t length,
                      rf_remote_config_t *config)
{
    if (encoded == NULL || config == NULL) return false;
    bool version1 = length == RF_REMOTE_CONFIG_V1_ENCODED_SIZE &&
                    encoded[0] == 1u && encoded[11] == 0xa5;
    bool version2 = length == RF_REMOTE_CONFIG_ENCODED_SIZE &&
                    encoded[0] == RF_REMOTE_CONFIG_VERSION && encoded[12] == 0xa5;
    if (!version1 && !version2) return false;
    rf_remote_config_t decoded;
    rf_config_defaults(&decoded);
    for (size_t i = 0; i < RF_CHANNEL_COUNT; ++i) {
        decoded.channel_map[i] = (remote_button_t)encoded[1 + i];
    }
    decoded.button4.type = (remote_action_type_t)encoded[5];
    decoded.button4.red = encoded[6];
    decoded.button4.green = encoded[7];
    decoded.button4.blue = encoded[8];
    decoded.button4.brightness = encoded[9];
    /* encoded[10] belonged to the removed Welcome action and is ignored. */
    if (version2) decoded.police_speed = (police_speed_t)encoded[11];
    if (!rf_config_mapping_valid(decoded.channel_map, false) ||
        !rf_config_button4_valid(&decoded.button4) ||
        !police_speed_valid(decoded.police_speed)) return false;
    *config = decoded;
    return true;
}

const char *remote_button_name(remote_button_t button)
{
    switch (button) {
    case REMOTE_BUTTON_1: return "BUTTON_A";
    case REMOTE_BUTTON_2: return "BUTTON_B";
    case REMOTE_BUTTON_3: return "BUTTON_C";
    case REMOTE_BUTTON_4: return "BUTTON_D";
    default: return "UNMAPPED";
    }
}

const char *remote_action_type_name(remote_action_type_t type)
{
    switch (type) {
    case REMOTE_ACTION_FAVORITE: return "favorite";
    case REMOTE_ACTION_RGB: return "rgb";
    case REMOTE_ACTION_WHITE: return "white";
    case REMOTE_ACTION_POLICE: return "police";
    default: return "invalid";
    }
}

remote_button_t rf_config_resolve_button(const rf_remote_config_t *config,
                                         rf_physical_channel_t channel,
                                         bool discovery_active)
{
    if (config == NULL || discovery_active || channel < RF_CHANNEL_D0 ||
        channel >= RF_CHANNEL_COUNT) return REMOTE_BUTTON_INVALID;
    remote_button_t button = config->channel_map[channel];
    return button >= REMOTE_BUTTON_1 && button < REMOTE_BUTTON_COUNT ?
           button : REMOTE_BUTTON_INVALID;
}

esp_err_t rf_config_init(void)
{
#ifdef ESP_PLATFORM
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
#endif
    rf_config_defaults(&s_config);
#ifdef ESP_PLATFORM
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint8_t encoded[RF_REMOTE_CONFIG_ENCODED_SIZE];
    size_t length = sizeof(encoded);
    err = nvs_get_blob(handle, NVS_CONFIG_KEY, encoded, &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    rf_remote_config_t loaded;
    if (!rf_config_decode(encoded, length, &loaded)) return ESP_ERR_INVALID_VERSION;
    lock(); s_config = loaded; unlock();
#endif
    return ESP_OK;
}

void rf_config_get(rf_remote_config_t *config)
{
    if (config == NULL) return;
    lock(); *config = s_config; unlock();
}

static esp_err_t save_config(const rf_remote_config_t *config)
{
    uint8_t encoded[RF_REMOTE_CONFIG_ENCODED_SIZE];
    if (!rf_config_encode(config, encoded)) return ESP_ERR_INVALID_ARG;
#ifdef ESP_PLATFORM
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, NVS_CONFIG_KEY, encoded, sizeof(encoded));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) return err;
#endif
    lock(); s_config = *config; unlock();
    return ESP_OK;
}

esp_err_t rf_config_save_button4(const remote_button4_config_t *button4)
{
    if (!rf_config_button4_valid(button4)) return ESP_ERR_INVALID_ARG;
    rf_remote_config_t updated;
    rf_config_get(&updated);
    updated.button4 = *button4;
    return save_config(&updated);
}

esp_err_t rf_config_save_police_speed(police_speed_t speed)
{
    if (!police_speed_valid(speed)) return ESP_ERR_INVALID_ARG;
    rf_remote_config_t updated;
    rf_config_get(&updated);
    updated.police_speed = speed;
    return save_config(&updated);
}

esp_err_t rf_config_save_mapping(const remote_button_t mapping[RF_CHANNEL_COUNT])
{
    if (!rf_config_mapping_valid(mapping, true)) return ESP_ERR_INVALID_ARG;
    rf_remote_config_t updated;
    rf_config_get(&updated);
    memcpy(updated.channel_map, mapping, sizeof(updated.channel_map));
    return save_config(&updated);
}
