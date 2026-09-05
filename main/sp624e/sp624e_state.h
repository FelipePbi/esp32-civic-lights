#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sp624e_protocol.h"

typedef struct {
    bool valid;
    bool power;
    uint8_t brightness;
    uint8_t speed;
    uint8_t chip_order;
    uint8_t effect;
    uint8_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t gain;
    uint8_t input;
    uint8_t white;
    uint8_t reserved_last;
    uint8_t raw_payload[SP624E_MESSAGE_MAX_LEN];
    size_t raw_payload_len;
    int64_t received_at_ms;
} sp624e_light_state_t;

typedef enum {
    SP624E_STATE_DIFF_NONE = 0,
    SP624E_STATE_DIFF_POWER = 1u << 0,
    SP624E_STATE_DIFF_BRIGHTNESS = 1u << 1,
    SP624E_STATE_DIFF_SPEED = 1u << 2,
    SP624E_STATE_DIFF_CHIP_ORDER = 1u << 3,
    SP624E_STATE_DIFF_EFFECT = 1u << 4,
    SP624E_STATE_DIFF_MODE = 1u << 5,
    SP624E_STATE_DIFF_RGB = 1u << 6,
    SP624E_STATE_DIFF_GAIN = 1u << 7,
    SP624E_STATE_DIFF_INPUT = 1u << 8,
    SP624E_STATE_DIFF_WHITE = 1u << 9,
    SP624E_STATE_DIFF_INVALID = 1u << 31,
} sp624e_state_diff_t;

sp624e_protocol_result_t sp624e_state_parse(const uint8_t *payload, size_t payload_len,
                                            int64_t received_at_ms,
                                            sp624e_light_state_t *state);
uint32_t sp624e_state_diff(const sp624e_light_state_t *expected,
                           const sp624e_light_state_t *observed);
bool sp624e_state_equal(const sp624e_light_state_t *expected,
                        const sp624e_light_state_t *observed);
