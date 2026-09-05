#include "group_types.h"

const char *sp624e_side_name(sp624e_side_t side)
{
    return side == SP624E_SIDE_LEFT ? "LEFT" :
           side == SP624E_SIDE_RIGHT ? "RIGHT" : "INVALID";
}

const char *sp624e_group_state_name(sp624e_group_state_t state)
{
    switch (state) {
    case SP624E_GROUP_UNINITIALIZED: return "UNINITIALIZED";
    case SP624E_GROUP_SYNCED: return "SYNCED";
    case SP624E_GROUP_DEGRADED: return "DEGRADED";
    case SP624E_GROUP_RECONCILING: return "RECONCILING";
    case SP624E_GROUP_POWER_CYCLE_RECOVERY: return "POWER_CYCLE_RECOVERY";
    case SP624E_GROUP_UNSYNCED: return "UNSYNCED";
    case SP624E_GROUP_ERROR: return "ERROR";
    default: return "INVALID";
    }
}

bool sp624e_group_generation_is_synced(uint32_t desired_generation,
                                       uint32_t left_verified_generation,
                                       uint32_t right_verified_generation,
                                       bool both_ready)
{
    return both_ready && desired_generation != 0 &&
           left_verified_generation == desired_generation &&
           right_verified_generation == desired_generation;
}
