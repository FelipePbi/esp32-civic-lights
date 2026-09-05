#include "animation_player.h"

static float clamp(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

float animation_ease(animation_easing_t easing, float progress)
{
    float p = clamp(progress);
    if (easing == ANIMATION_EASING_IN_OUT) return p * p * (3.0f - 2.0f * p);
    if (easing == ANIMATION_EASING_OUT) {
        float inverse = 1.0f - p;
        return 1.0f - inverse * inverse;
    }
    return p;
}

uint8_t animation_lerp_u8(uint8_t from, uint8_t to, float progress)
{
    float value = (float)from + ((float)to - (float)from) * clamp(progress);
    if (value <= 0.0f) return 0;
    if (value >= 255.0f) return 255;
    return (uint8_t)(value + 0.5f);
}

uint32_t animation_scale_elapsed(uint32_t elapsed_ms, uint32_t target_duration_ms,
                                 uint32_t source_duration_ms)
{
    if (target_duration_ms == 0 || elapsed_ms >= target_duration_ms) {
        return source_duration_ms;
    }
    return (uint32_t)(((uint64_t)elapsed_ms * source_duration_ms) /
                      target_duration_ms);
}

bool animation_sample(const animation_keyframe_t *keyframes, size_t count,
                      uint32_t elapsed_ms, animation_frame_t *frame)
{
    if (keyframes == NULL || frame == NULL || count == 0) return false;
    size_t end = 0;
    while (end < count && elapsed_ms > keyframes[end].time_ms) end++;
    if (end == 0) {
        const animation_keyframe_t *key = &keyframes[0];
        *frame = (animation_frame_t){
            .mode = key->mode, .red = key->red, .green = key->green,
            .blue = key->blue, .brightness = key->brightness,
            .transition_barrier = false,
        };
        return true;
    }
    if (end == count) {
        const animation_keyframe_t *key = &keyframes[count - 1];
        *frame = (animation_frame_t){
            .mode = key->mode, .red = key->red, .green = key->green,
            .blue = key->blue, .brightness = key->brightness,
            .transition_barrier = false,
        };
        return true;
    }
    const animation_keyframe_t *from = &keyframes[end - 1];
    const animation_keyframe_t *to = &keyframes[end];
    uint32_t span = to->time_ms - from->time_ms;
    float progress = span == 0 ? 1.0f : (float)(elapsed_ms - from->time_ms) / span;
    progress = animation_ease(to->easing, progress);
    /* Mode changes at midpoint. This avoids RGB 255,255,255 as fake white. */
    frame->mode = progress < 0.5f ? from->mode : to->mode;
    frame->red = animation_lerp_u8(from->red, to->red, progress);
    frame->green = animation_lerp_u8(from->green, to->green, progress);
    frame->blue = animation_lerp_u8(from->blue, to->blue, progress);
    frame->brightness = animation_lerp_u8(from->brightness, to->brightness, progress);
    frame->transition_barrier = false;
    return true;
}
