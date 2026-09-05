#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SP624E_SIDE_LEFT = 0,
    SP624E_SIDE_RIGHT = 1,
    SP624E_SIDE_COUNT = 2,
} sp624e_side_t;

typedef enum {
    SP624E_GROUP_UNINITIALIZED = 0,
    SP624E_GROUP_SYNCED,
    SP624E_GROUP_DEGRADED,
    SP624E_GROUP_RECONCILING,
    SP624E_GROUP_POWER_CYCLE_RECOVERY,
    SP624E_GROUP_UNSYNCED,
    SP624E_GROUP_ERROR,
} sp624e_group_state_t;

const char *sp624e_side_name(sp624e_side_t side);
const char *sp624e_group_state_name(sp624e_group_state_t state);
bool sp624e_group_generation_is_synced(uint32_t desired_generation,
                                       uint32_t left_verified_generation,
                                       uint32_t right_verified_generation,
                                       bool both_ready);
