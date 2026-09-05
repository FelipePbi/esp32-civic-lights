#include "sp624e_state.h"

#include <string.h>

sp624e_protocol_result_t sp624e_state_parse(const uint8_t *payload, size_t payload_len,
                                            int64_t received_at_ms,
                                            sp624e_light_state_t *state)
{
    if (payload == NULL || state == NULL) {
        return SP624E_PROTOCOL_INVALID_ARGUMENT;
    }
    memset(state, 0, sizeof(*state));
    if (payload_len < 13) {
        return SP624E_PROTOCOL_INVALID_PACKET;
    }
    if (payload_len > sizeof(state->raw_payload)) {
        return SP624E_PROTOCOL_OVERSIZED;
    }
    if (payload[0] > 1 || payload[2] > 10 || payload[5] > 2) {
        return SP624E_PROTOCOL_INVALID_VALUE;
    }

    state->power = payload[0] == 1;
    state->brightness = payload[1];
    state->speed = payload[2];
    state->chip_order = payload[3];
    state->effect = payload[4];
    state->mode = payload[5];
    state->red = payload[6];
    state->green = payload[7];
    state->blue = payload[8];
    state->gain = payload[9];
    state->input = payload[payload_len - 3];
    state->white = payload[payload_len - 2];
    state->reserved_last = payload[payload_len - 1];
    memcpy(state->raw_payload, payload, payload_len);
    state->raw_payload_len = payload_len;
    state->received_at_ms = received_at_ms;
    state->valid = true;
    return SP624E_PROTOCOL_OK;
}

uint32_t sp624e_state_diff(const sp624e_light_state_t *expected,
                           const sp624e_light_state_t *observed)
{
    if (expected == NULL || observed == NULL || !expected->valid || !observed->valid) {
        return (uint32_t)SP624E_STATE_DIFF_INVALID;
    }
    uint32_t diff = SP624E_STATE_DIFF_NONE;
    if (expected->power != observed->power) diff |= SP624E_STATE_DIFF_POWER;
    if (expected->brightness != observed->brightness) diff |= SP624E_STATE_DIFF_BRIGHTNESS;
    if (expected->speed != observed->speed) diff |= SP624E_STATE_DIFF_SPEED;
    if (expected->chip_order != observed->chip_order) diff |= SP624E_STATE_DIFF_CHIP_ORDER;
    if (expected->effect != observed->effect) diff |= SP624E_STATE_DIFF_EFFECT;
    if (expected->mode != observed->mode) diff |= SP624E_STATE_DIFF_MODE;
    if (expected->red != observed->red || expected->green != observed->green ||
        expected->blue != observed->blue) {
        diff |= SP624E_STATE_DIFF_RGB;
    }
    if (expected->gain != observed->gain) diff |= SP624E_STATE_DIFF_GAIN;
    if (expected->input != observed->input) diff |= SP624E_STATE_DIFF_INPUT;
    if (expected->white != observed->white) diff |= SP624E_STATE_DIFF_WHITE;
    return diff;
}

bool sp624e_state_equal(const sp624e_light_state_t *expected,
                        const sp624e_light_state_t *observed)
{
    if (expected == NULL || observed == NULL) {
        return false;
    }
    if (!expected->valid || !observed->valid) {
        return expected->valid == observed->valid;
    }
    return sp624e_state_diff(expected, observed) == SP624E_STATE_DIFF_NONE;
}
