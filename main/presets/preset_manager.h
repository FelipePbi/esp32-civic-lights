#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SP624E_FAVORITE_VERSION 1u

typedef struct {
    uint32_t version;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
} sp624e_favorite_preset_t;

esp_err_t preset_manager_init(void);
void preset_manager_get_favorite(sp624e_favorite_preset_t *preset);
esp_err_t preset_manager_save_favorite(const sp624e_favorite_preset_t *preset);
bool preset_manager_white_available(void);
esp_err_t preset_manager_set_white_available(bool available);
