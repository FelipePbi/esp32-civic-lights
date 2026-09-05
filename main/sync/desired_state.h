#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#endif
#include "sp624e/sp624e_state.h"

#define SP624E_DESIRED_STATE_VERSION 2u

typedef enum {
    SP624E_LIGHT_MODE_RGB = 0,
    SP624E_LIGHT_MODE_WHITE = 1,
} sp624e_light_mode_t;

typedef struct {
    bool valid;
    uint32_t generation;
    bool power;
    sp624e_light_mode_t light_mode;
    uint8_t effect;
    uint8_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    uint8_t white;
    uint8_t speed;
} sp624e_desired_state_t;

static inline bool sp624e_desired_needs_persistence(
    const sp624e_desired_state_t *desired, uint32_t persisted_generation,
    bool temporary, bool already_scheduled)
{
    return desired != NULL && desired->valid && !temporary && !already_scheduled &&
           desired->generation != persisted_generation;
}

void sp624e_desired_from_observed(sp624e_desired_state_t *desired,
                                  const sp624e_light_state_t *observed,
                                  uint32_t generation);
void sp624e_desired_set_rgb(sp624e_desired_state_t *desired, uint8_t red,
                            uint8_t green, uint8_t blue, uint8_t brightness);
void sp624e_desired_set_white(sp624e_desired_state_t *desired, uint8_t level);
esp_err_t sp624e_desired_load(sp624e_desired_state_t *desired, bool *restore_on_boot);
esp_err_t sp624e_desired_save(const sp624e_desired_state_t *desired,
                              bool restore_on_boot);
