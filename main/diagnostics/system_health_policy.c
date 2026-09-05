#include "system_health_policy.h"

bool system_health_heartbeat_stale(int64_t heartbeat_ms, int64_t now_ms,
                                   int64_t stale_after_ms)
{
    if (heartbeat_ms <= 0 || stale_after_ms < 0) return true;
    if (now_ms < heartbeat_ms) return false;
    return now_ms - heartbeat_ms > stale_after_ms;
}

bool system_health_restart_due(int64_t stale_since_ms, int64_t now_ms,
                               int64_t restart_after_ms)
{
    if (stale_since_ms <= 0 || restart_after_ms < 0 || now_ms < stale_since_ms) {
        return false;
    }
    return now_ms - stale_since_ms >= restart_after_ms;
}
