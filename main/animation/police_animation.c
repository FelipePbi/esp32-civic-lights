#include "police_animation.h"

#include <string.h>

typedef struct {
    uint16_t color_ms;
    uint16_t blackout_ms;
} police_timing_t;

static const police_timing_t timings[POLICE_SPEED_COUNT] = {
    [POLICE_SPEED_SLOW] = {400, 200},
    [POLICE_SPEED_NORMAL] = {300, 150},
    [POLICE_SPEED_FAST] = {220, 150},
    [POLICE_SPEED_VERY_FAST] = {150, 150},
};

bool police_speed_valid(police_speed_t speed)
{
    return speed >= POLICE_SPEED_SLOW && speed < POLICE_SPEED_COUNT;
}

const char *police_speed_name(police_speed_t speed)
{
    switch (speed) {
    case POLICE_SPEED_SLOW: return "slow";
    case POLICE_SPEED_NORMAL: return "normal";
    case POLICE_SPEED_FAST: return "fast";
    case POLICE_SPEED_VERY_FAST: return "very_fast";
    default: return "invalid";
    }
}

police_speed_t police_speed_from_name(const char *name)
{
    if (name == NULL) return POLICE_SPEED_COUNT;
    for (police_speed_t speed = POLICE_SPEED_SLOW;
         speed < POLICE_SPEED_COUNT; speed++) {
        if (strcmp(name, police_speed_name(speed)) == 0) return speed;
    }
    return POLICE_SPEED_COUNT;
}

uint16_t police_animation_cycle_ms(police_speed_t speed)
{
    if (!police_speed_valid(speed)) return 0;
    const police_timing_t *timing = &timings[speed];
    return 3u * (timing->color_ms + timing->blackout_ms);
}

uint16_t police_animation_phase_duration_ms(police_speed_t speed, uint8_t phase)
{
    if (!police_speed_valid(speed) || phase >= POLICE_PATTERN_PHASE_COUNT) return 0;
    const police_timing_t *timing = &timings[speed];
    return (phase & 1u) == 0 ? timing->color_ms : timing->blackout_ms;
}

bool police_animation_phase_frame(uint8_t phase, animation_frame_t *frame)
{
    if (frame == NULL || phase >= POLICE_PATTERN_PHASE_COUNT) return false;
    bool red = phase == 0 || phase == 2;
    bool blue = phase == 4;
    *frame = (animation_frame_t) {
        .mode = ANIMATION_MODE_RGB,
        .red = red ? 255 : 0,
        .green = 0,
        .blue = blue ? 255 : 0,
        /* Keep the controller at full brightness during black frames. RGB=0
         * provides the blackout without forcing a 0 -> 255 brightness ramp. */
        .brightness = POLICE_PATTERN_BRIGHTNESS,
        .transition_barrier = true,
    };
    return true;
}

bool police_animation_sample(uint32_t elapsed_ms, police_speed_t speed,
                             animation_frame_t *frame, uint8_t *phase)
{
    if (frame == NULL || phase == NULL || !police_speed_valid(speed)) return false;
    const police_timing_t *timing = &timings[speed];
    uint32_t position = elapsed_ms % police_animation_cycle_ms(speed);
    uint32_t pair_ms = timing->color_ms + timing->blackout_ms;
    uint8_t pair = (uint8_t)(position / pair_ms);
    uint8_t selected = (uint8_t)(pair * 2u +
        ((position % pair_ms) >= timing->color_ms ? 1u : 0u));
    *phase = selected;
    return police_animation_phase_frame(selected, frame);
}
