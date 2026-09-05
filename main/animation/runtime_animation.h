#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RUNTIME_ANIMATION_IDLE = 0,
    RUNTIME_ANIMATION_STARTING,
    RUNTIME_ANIMATION_RUNNING,
    RUNTIME_ANIMATION_CANCELLING,
    RUNTIME_ANIMATION_FAILED,
} runtime_animation_state_t;

typedef struct {
    runtime_animation_state_t state;
    uint32_t generation;
    uint32_t elapsed_ms;
    uint32_t generated_frames;
    uint32_t accepted_frames;
    uint32_t dropped_frames;
    bool timed_out;
} runtime_animation_snapshot_t;

typedef void (*runtime_animation_event_fn)(const char *event);

void runtime_animation_init(runtime_animation_event_fn event_fn);
bool runtime_animation_start_police(void);
bool runtime_animation_toggle_police(void);
bool runtime_animation_stop(void);
void runtime_animation_cancel_for_user(void);
void runtime_animation_on_disconnect(void);
void runtime_animation_get_snapshot(runtime_animation_snapshot_t *snapshot);
bool runtime_animation_is_active(void);
const char *runtime_animation_state_name(runtime_animation_state_t state);
