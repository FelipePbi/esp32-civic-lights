#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ANIMATION_MODE_RGB = 0,
    ANIMATION_MODE_WHITE,
} animation_light_mode_t;

typedef enum {
    ANIMATION_EASING_LINEAR = 0,
    ANIMATION_EASING_IN_OUT,
    ANIMATION_EASING_OUT,
} animation_easing_t;

typedef struct {
    animation_light_mode_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    uint32_t time_ms;
    animation_easing_t easing;
} animation_keyframe_t;

typedef struct {
    animation_light_mode_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    bool transition_barrier;
} animation_frame_t;

float animation_ease(animation_easing_t easing, float progress);
uint8_t animation_lerp_u8(uint8_t from, uint8_t to, float progress);
uint32_t animation_scale_elapsed(uint32_t elapsed_ms, uint32_t target_duration_ms,
                                 uint32_t source_duration_ms);
bool animation_sample(const animation_keyframe_t *keyframes, size_t count,
                      uint32_t elapsed_ms, animation_frame_t *frame);
