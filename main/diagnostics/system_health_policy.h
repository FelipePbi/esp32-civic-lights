#pragma once

#include <stdbool.h>
#include <stdint.h>

bool system_health_heartbeat_stale(int64_t heartbeat_ms, int64_t now_ms,
                                   int64_t stale_after_ms);
bool system_health_restart_due(int64_t stale_since_ms, int64_t now_ms,
                               int64_t restart_after_ms);
