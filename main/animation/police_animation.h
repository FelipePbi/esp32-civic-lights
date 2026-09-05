#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "animation_player.h"

#define POLICE_PATTERN_PHASE_COUNT 6u
#define POLICE_PATTERN_BRIGHTNESS 255u

typedef enum {
    POLICE_SPEED_SLOW = 0,
    POLICE_SPEED_NORMAL,
    POLICE_SPEED_FAST,
    POLICE_SPEED_VERY_FAST,
    POLICE_SPEED_COUNT,
} police_speed_t;

bool police_speed_valid(police_speed_t speed);
const char *police_speed_name(police_speed_t speed);
police_speed_t police_speed_from_name(const char *name);
uint16_t police_animation_cycle_ms(police_speed_t speed);
uint16_t police_animation_phase_duration_ms(police_speed_t speed, uint8_t phase);
bool police_animation_phase_frame(uint8_t phase, animation_frame_t *frame);
bool police_animation_sample(uint32_t elapsed_ms, police_speed_t speed,
                             animation_frame_t *frame, uint8_t *phase);
