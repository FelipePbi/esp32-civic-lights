#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rf_config.h"

typedef enum {
    REMOTE_INTENT_WHITE = 0,
    REMOTE_INTENT_RGB,
    REMOTE_INTENT_FAVORITE,
    REMOTE_INTENT_POLICE_TOGGLE,
} remote_intent_type_t;

typedef struct {
    remote_intent_type_t type;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
} remote_action_plan_t;

bool remote_action_plan(remote_button_t button,
                        const remote_button4_config_t *button4,
                        remote_action_plan_t *plan);
bool remote_action_can_execute(const remote_action_plan_t *plan,
                               bool controller_started,
                               bool both_ready,
                               bool group_synced,
                               bool white_available);
