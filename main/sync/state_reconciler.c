#include "state_reconciler.h"

#include <string.h>

#include "sp624e/sp624e_protocol.h"

bool sp624e_visual_equivalent(const sp624e_light_state_t *left,
                              const sp624e_light_state_t *right)
{
    if (left == NULL || right == NULL || !left->valid || !right->valid) return false;
    if (left->power != right->power || left->effect != right->effect ||
        left->mode != right->mode) return false;
    if (left->effect == SP624E_EFFECT_WHITE) return left->white == right->white;
    if (left->effect == SP624E_EFFECT_SOLID) {
        return left->brightness == right->brightness && left->red == right->red &&
               left->green == right->green && left->blue == right->blue;
    }
    return left->brightness == right->brightness && left->red == right->red &&
           left->green == right->green && left->blue == right->blue &&
           left->white == right->white;
}

bool sp624e_reconciler_matches(const sp624e_desired_state_t *desired,
                               const sp624e_light_state_t *observed)
{
    if (desired == NULL || observed == NULL || !desired->valid || !observed->valid) {
        return false;
    }
    if (desired->power != observed->power || desired->mode != observed->mode) return false;
    if (desired->light_mode == SP624E_LIGHT_MODE_WHITE) {
        return observed->effect == SP624E_EFFECT_WHITE &&
               desired->white == observed->white;
    }
    return observed->effect == SP624E_EFFECT_SOLID &&
           desired->brightness == observed->brightness &&
           desired->red == observed->red && desired->green == observed->green &&
           desired->blue == observed->blue;
}

sp624e_reconcile_plan_t sp624e_reconciler_plan(
    const sp624e_desired_state_t *desired,
    const sp624e_light_state_t *observed)
{
    sp624e_reconcile_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    if (desired == NULL || observed == NULL || !desired->valid || !observed->valid) {
        return plan;
    }
    plan.valid = true;
    if (sp624e_reconciler_matches(desired, observed)) {
        plan.already_synchronized = true;
        return plan;
    }

    if (desired->power != observed->power || desired->mode != observed->mode) {
        plan.unsupported_difference = true;
        return plan;
    }
    if (desired->light_mode == SP624E_LIGHT_MODE_WHITE) {
        if (observed->effect != SP624E_EFFECT_WHITE) {
            plan.commands[plan.count++].type = SP624E_RECONCILE_CMD_EFFECT_WHITE;
        }
        if (observed->effect != SP624E_EFFECT_WHITE || desired->white != observed->white) {
            sp624e_reconcile_command_t *command = &plan.commands[plan.count++];
            command->type = SP624E_RECONCILE_CMD_WHITE;
            command->level = desired->white;
        }
        return plan;
    }
    bool rgb_diff = desired->red != observed->red || desired->green != observed->green ||
                    desired->blue != observed->blue;
    if (observed->effect != SP624E_EFFECT_SOLID) {
        /* Preload the target RGB while the previous effect is still active.
         * Switching to SOLID first exposes the controller's remembered RGB
         * (often the final red Police frame) until the next GATT write. */
        sp624e_reconcile_command_t *command = &plan.commands[plan.count++];
        command->type = SP624E_RECONCILE_CMD_RGB;
        command->red = desired->red;
        command->green = desired->green;
        command->blue = desired->blue;
        command->level = desired->brightness;
        plan.commands[plan.count++].type = SP624E_RECONCILE_CMD_EFFECT_SOLID;
        return plan;
    }
    if (rgb_diff) {
        sp624e_reconcile_command_t *command = &plan.commands[plan.count++];
        command->type = SP624E_RECONCILE_CMD_RGB;
        command->red = desired->red;
        command->green = desired->green;
        command->blue = desired->blue;
        command->level = desired->brightness;
    } else if (desired->brightness != observed->brightness) {
        sp624e_reconcile_command_t *command = &plan.commands[plan.count++];
        command->type = SP624E_RECONCILE_CMD_BRIGHTNESS;
        command->level = desired->brightness;
    }
    return plan;
}

sp624e_reconcile_plan_t sp624e_reconciler_force_plan(
    const sp624e_desired_state_t *desired)
{
    sp624e_reconcile_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    if (desired == NULL || !desired->valid) return plan;
    plan.valid = true;
    if (desired->light_mode == SP624E_LIGHT_MODE_WHITE) {
        plan.commands[plan.count++].type = SP624E_RECONCILE_CMD_EFFECT_WHITE;
        sp624e_reconcile_command_t *command = &plan.commands[plan.count++];
        command->type = SP624E_RECONCILE_CMD_WHITE;
        command->level = desired->white;
    } else {
        sp624e_reconcile_command_t *command = &plan.commands[plan.count++];
        command->type = SP624E_RECONCILE_CMD_RGB;
        command->red = desired->red;
        command->green = desired->green;
        command->blue = desired->blue;
        command->level = desired->brightness;
        plan.commands[plan.count++].type = SP624E_RECONCILE_CMD_EFFECT_SOLID;
    }
    return plan;
}
