#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "desired_state.h"

typedef enum {
    SP624E_RECONCILE_CMD_EFFECT_SOLID = 0,
    SP624E_RECONCILE_CMD_EFFECT_WHITE,
    SP624E_RECONCILE_CMD_RGB,
    SP624E_RECONCILE_CMD_BRIGHTNESS,
    SP624E_RECONCILE_CMD_WHITE,
} sp624e_reconcile_command_type_t;

typedef struct {
    sp624e_reconcile_command_type_t type;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t level;
} sp624e_reconcile_command_t;

typedef struct {
    bool valid;
    bool already_synchronized;
    bool unsupported_difference;
    size_t count;
    sp624e_reconcile_command_t commands[4];
} sp624e_reconcile_plan_t;

bool sp624e_visual_equivalent(const sp624e_light_state_t *left,
                              const sp624e_light_state_t *right);
bool sp624e_reconciler_matches(const sp624e_desired_state_t *desired,
                               const sp624e_light_state_t *observed);
sp624e_reconcile_plan_t sp624e_reconciler_plan(
    const sp624e_desired_state_t *desired,
    const sp624e_light_state_t *observed);
sp624e_reconcile_plan_t sp624e_reconciler_force_plan(
    const sp624e_desired_state_t *desired);
