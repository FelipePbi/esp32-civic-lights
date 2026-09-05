#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ble/ble_connection.h"
#include "ble/ble_connection_manager.h"
#include "diagnostics/connection_metrics.h"
#include "sp624e_state.h"
#include "sync/desired_state.h"
#include "sync/group_types.h"
#include "animation/animation_player.h"

struct sp624e_favorite_preset;

typedef enum {
    SP624E_GROUP_API_OK = 0,
    SP624E_GROUP_API_NOT_READY,
    SP624E_GROUP_API_BUSY,
    SP624E_GROUP_API_TIMEOUT,
    SP624E_GROUP_API_UNSUPPORTED,
} sp624e_group_api_result_t;

typedef struct {
    ble_connection_manager_status_t connection;
    sp624e_light_state_t observed;
    sp624e_connection_metrics_t metrics;
    uint32_t applied_generation;
    uint32_t verified_generation;
} sp624e_side_snapshot_t;

typedef struct {
    bool controller_started;
    bool white_available;
    sp624e_group_state_t group_state;
    sp624e_desired_state_t desired;
    sp624e_side_snapshot_t sides[SP624E_SIDE_COUNT];
    uint32_t animation_frames_sent[SP624E_SIDE_COUNT];
    uint32_t animation_coalesced_commands;
    uint32_t animation_max_queue_depth;
} sp624e_group_snapshot_t;

typedef struct {
    int64_t heartbeat_ms;
    uint32_t api_timeouts;
    uint32_t api_busy;
    uint32_t api_response_drops;
} sp624e_runtime_health_t;

void sp624e_controller_start(const sp624e_transport_t transports[2]);
bool sp624e_controller_is_started(void);
sp624e_group_api_result_t sp624e_group_set_rgb(uint8_t red, uint8_t green,
                                               uint8_t blue, uint8_t brightness,
                                               uint32_t *accepted_generation);
sp624e_group_api_result_t sp624e_group_set_white(uint8_t level,
                                                 uint32_t *accepted_generation);
sp624e_group_api_result_t sp624e_group_force_resync(uint32_t *accepted_generation);
sp624e_group_api_result_t sp624e_group_apply_favorite(uint32_t *accepted_generation);
void sp624e_group_get_snapshot(sp624e_group_snapshot_t *snapshot);
bool sp624e_group_animation_begin(uint32_t animation_generation);
bool sp624e_group_animation_frame_ready(uint32_t animation_generation);
bool sp624e_group_animation_frame(uint32_t animation_generation,
                                  const animation_frame_t *frame);
void sp624e_group_animation_end(uint32_t animation_generation);
void sp624e_controller_on_notification(uint16_t conn_handle, uint16_t attr_handle,
                                       const uint8_t *data, size_t length);
void sp624e_controller_on_disconnect(const ble_addr_t *address, int reason);
void sp624e_controller_on_fast_recovery(sp624e_side_t side);
void sp624e_controller_on_recovered(sp624e_side_t side,
                                    const sp624e_transport_t *transport);
void sp624e_controller_get_runtime_health(sp624e_runtime_health_t *health);
