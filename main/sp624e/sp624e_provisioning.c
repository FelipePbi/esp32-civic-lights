#include "sp624e_provisioning.h"

#include <string.h>

#include "nvs.h"

#define SP624E_NVS_NAMESPACE "sp624e"
#define SP624E_NVS_MAPPING_KEY "mapping"
#define SP624E_NVS_SYNC_DONE_KEY "sync_done"

esp_err_t sp624e_mapping_load(sp624e_mapping_t *mapping)
{
    if (mapping == NULL) return ESP_ERR_INVALID_ARG;
    memset(mapping, 0, sizeof(*mapping));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SP624E_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    uint8_t encoded[SP624E_MAPPING_ENCODED_LEN];
    size_t length = sizeof(encoded);
    err = nvs_get_blob(handle, SP624E_NVS_MAPPING_KEY, encoded, &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    return sp624e_mapping_decode(encoded, length, mapping) == 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t sp624e_mapping_save(const sp624e_mapping_t *mapping)
{
    uint8_t encoded[SP624E_MAPPING_ENCODED_LEN];
    size_t length = 0;
    if (sp624e_mapping_encode(mapping, encoded, sizeof(encoded), &length) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SP624E_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, SP624E_NVS_MAPPING_KEY, encoded, length);
    if (err == ESP_OK) err = nvs_set_u8(handle, SP624E_NVS_SYNC_DONE_KEY, 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t sp624e_mapping_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SP624E_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t sp624e_sync_test_done_load(bool *done)
{
    if (done == NULL) return ESP_ERR_INVALID_ARG;
    *done = false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SP624E_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint8_t value = 0;
    err = nvs_get_u8(handle, SP624E_NVS_SYNC_DONE_KEY, &value);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    *done = value != 0;
    return ESP_OK;
}

esp_err_t sp624e_sync_test_done_save(bool done)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SP624E_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, SP624E_NVS_SYNC_DONE_KEY, done ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
