#include "indicator_policy.h"

static bool observed_special(const sp624e_light_state_t *state)
{
    return state->valid && state->power && state->effect != SP624E_EFFECT_WHITE;
}

static bool observed_white_or_off(const sp624e_light_state_t *state)
{
    return state->valid && (!state->power || state->effect == SP624E_EFFECT_WHITE);
}

indicator_decision_t indicator_policy_evaluate(
    bool previous_on, bool animation_active, bool physical_group_ready,
    sp624e_group_state_t group_state,
    const sp624e_desired_state_t *desired,
    const sp624e_light_state_t *left, const sp624e_light_state_t *right)
{
    if (animation_active && physical_group_ready) {
        return (indicator_decision_t){true, INDICATOR_REASON_ANIMATION};
    }
    bool known = left != NULL && right != NULL && left->valid && right->valid;
    if (group_state == SP624E_GROUP_SYNCED && known) {
        if (observed_white_or_off(left) && observed_white_or_off(right)) {
            return (indicator_decision_t){false, INDICATOR_REASON_CONFIRMED_WHITE};
        }
        if (observed_special(left) || observed_special(right)) {
            return (indicator_decision_t){true, INDICATOR_REASON_CONFIRMED_SPECIAL};
        }
    }
    if (group_state == SP624E_GROUP_DEGRADED ||
        group_state == SP624E_GROUP_UNINITIALIZED ||
        group_state == SP624E_GROUP_ERROR || !known) {
        return (indicator_decision_t){false, INDICATOR_REASON_UNKNOWN};
    }
    if (desired != NULL && desired->valid && desired->power &&
        desired->light_mode != SP624E_LIGHT_MODE_WHITE) {
        return (indicator_decision_t){true, INDICATOR_REASON_APPLYING_SPECIAL};
    }
    if (desired != NULL && desired->valid &&
        desired->light_mode == SP624E_LIGHT_MODE_WHITE) {
        return (indicator_decision_t){previous_on, INDICATOR_REASON_WAITING_WHITE};
    }
    return (indicator_decision_t){false, INDICATOR_REASON_UNKNOWN};
}

const char *indicator_reason_name(indicator_reason_t reason)
{
    switch (reason) {
    case INDICATOR_REASON_BOOT: return "BOOT";
    case INDICATOR_REASON_ANIMATION: return "ANIMATION";
    case INDICATOR_REASON_CONFIRMED_WHITE: return "CONFIRMED_WHITE";
    case INDICATOR_REASON_CONFIRMED_SPECIAL: return "CONFIRMED_SPECIAL";
    case INDICATOR_REASON_APPLYING_SPECIAL: return "APPLYING_SPECIAL";
    case INDICATOR_REASON_WAITING_WHITE: return "WAITING_WHITE";
    case INDICATOR_REASON_UNKNOWN: return "UNKNOWN";
    case INDICATOR_REASON_SELF_TEST: return "SELF_TEST";
    default: return "UNKNOWN";
    }
}
