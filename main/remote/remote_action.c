#include "remote_action.h"

#include <string.h>

#include "app_config.h"

bool remote_action_plan(remote_button_t button,
                        const remote_button4_config_t *button4,
                        remote_action_plan_t *plan)
{
    if (plan == NULL) return false;
    memset(plan, 0, sizeof(*plan));
    if (button == REMOTE_BUTTON_1) {
        plan->type = REMOTE_INTENT_WHITE;
        plan->brightness = APP_REMOTE_WHITE_BRIGHTNESS;
        return true;
    }
    if (button == REMOTE_BUTTON_2) {
        plan->type = REMOTE_INTENT_RGB;
        plan->red = 255;
        plan->brightness = APP_REMOTE_RED_BRIGHTNESS;
        return true;
    }
    if (button == REMOTE_BUTTON_3) {
        plan->type = REMOTE_INTENT_POLICE_TOGGLE;
        return true;
    }
    if (button != REMOTE_BUTTON_4 || !rf_config_button4_valid(button4)) return false;
    plan->red = button4->red;
    plan->green = button4->green;
    plan->blue = button4->blue;
    plan->brightness = button4->brightness;
    switch (button4->type) {
    case REMOTE_ACTION_FAVORITE: plan->type = REMOTE_INTENT_FAVORITE; return true;
    case REMOTE_ACTION_RGB: plan->type = REMOTE_INTENT_RGB; return true;
    case REMOTE_ACTION_WHITE: plan->type = REMOTE_INTENT_WHITE; return true;
    case REMOTE_ACTION_POLICE: plan->type = REMOTE_INTENT_POLICE_TOGGLE; return true;
    default: return false;
    }
}

bool remote_action_can_execute(const remote_action_plan_t *plan,
                               bool controller_started,
                               bool both_ready,
                               bool group_synced,
                               bool white_available)
{
    if (plan == NULL || !controller_started) return false;
    if (plan->type == REMOTE_INTENT_WHITE && !white_available) return false;
    if (plan->type == REMOTE_INTENT_POLICE_TOGGLE &&
        (!both_ready || !group_synced)) return false;
    return true;
}
