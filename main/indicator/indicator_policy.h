#pragma once

#include <stdbool.h>

#include "sp624e/sp624e_state.h"
#include "sync/desired_state.h"
#include "sync/group_types.h"

typedef enum {
    INDICATOR_REASON_BOOT = 0,
    INDICATOR_REASON_ANIMATION,
    INDICATOR_REASON_CONFIRMED_WHITE,
    INDICATOR_REASON_CONFIRMED_SPECIAL,
    INDICATOR_REASON_APPLYING_SPECIAL,
    INDICATOR_REASON_WAITING_WHITE,
    INDICATOR_REASON_UNKNOWN,
    INDICATOR_REASON_SELF_TEST,
} indicator_reason_t;

typedef struct {
    bool on;
    indicator_reason_t reason;
} indicator_decision_t;

indicator_decision_t indicator_policy_evaluate(
    bool previous_on, bool animation_active, bool physical_group_ready,
    sp624e_group_state_t group_state,
    const sp624e_desired_state_t *desired,
    const sp624e_light_state_t *left, const sp624e_light_state_t *right);
const char *indicator_reason_name(indicator_reason_t reason);
