#include "desired_state.h"

#include <string.h>

#include "sp624e/sp624e_protocol.h"

static uint32_t next_generation(uint32_t current)
{
    uint32_t next = current + 1;
    return next == 0 ? 1 : next;
}

void sp624e_desired_from_observed(sp624e_desired_state_t *desired,
                                  const sp624e_light_state_t *observed,
                                  uint32_t generation)
{
    if (desired == NULL) return;
    memset(desired, 0, sizeof(*desired));
    if (observed == NULL || !observed->valid) return;
    desired->valid = true;
    desired->generation = generation == 0 ? 1 : generation;
    desired->power = observed->power;
    desired->light_mode = observed->effect == SP624E_EFFECT_WHITE ?
                          SP624E_LIGHT_MODE_WHITE : SP624E_LIGHT_MODE_RGB;
    desired->effect = observed->effect;
    desired->mode = observed->mode;
    desired->red = observed->red;
    desired->green = observed->green;
    desired->blue = observed->blue;
    desired->brightness = observed->brightness;
    desired->white = observed->white;
    desired->speed = observed->speed;
}

void sp624e_desired_set_rgb(sp624e_desired_state_t *desired, uint8_t red,
                            uint8_t green, uint8_t blue, uint8_t brightness)
{
    if (desired == NULL) return;
    desired->valid = true;
    desired->generation = next_generation(desired->generation);
    desired->power = true;
    desired->light_mode = SP624E_LIGHT_MODE_RGB;
    desired->effect = SP624E_EFFECT_SOLID;
    desired->red = red;
    desired->green = green;
    desired->blue = blue;
    desired->brightness = brightness;
}

void sp624e_desired_set_white(sp624e_desired_state_t *desired, uint8_t level)
{
    if (desired == NULL) return;
    desired->valid = true;
    desired->generation = next_generation(desired->generation);
    desired->power = true;
    desired->light_mode = SP624E_LIGHT_MODE_WHITE;
    desired->effect = SP624E_EFFECT_WHITE;
    desired->white = level;
    desired->brightness = level;
}
