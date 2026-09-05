#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "interior_light_policy.h"
#include "sync/desired_state.h"

/*
 * LEDCAR-00-1900 interior lighting, best effort.
 *
 * The controller is write-only in practice: READ is rejected, NOTIFY never
 * fires and writes carry no acknowledgement. There is therefore NO observed
 * state and no verification. This module tracks intent only.
 *
 * It never blocks a caller, never participates in Strict Sync or generation,
 * never delays the PWA, and always yields the BLE master role to SP624E
 * recovery. A failure here is invisible to LEFT and RIGHT.
 *
 * No dedicated task: interior_light_service() is driven by an existing runtime
 * loop and every step is non-blocking.
 */

typedef enum {
    INTERIOR_LIGHT_IDLE = 0,     /* disconnected, nothing pending */
    INTERIOR_LIGHT_PENDING,      /* colour pending, waiting for a safe window */
    INTERIOR_LIGHT_CONNECTING,
    INTERIOR_LIGHT_DISCOVERING,
    INTERIOR_LIGHT_CONNECTED,
    INTERIOR_LIGHT_BACKOFF,
} interior_light_state_t;

typedef struct {
    interior_light_state_t state;
    bool connected;
    interior_rgb_t desired;
    interior_rgb_t last_attempted;
    bool last_attempt_valid;
    int64_t last_write_ms;
    uint32_t write_attempts;
    uint32_t connect_attempts;
    uint32_t connect_failures;
    uint32_t backoff_ms;
    int last_disconnect_reason;
} interior_light_snapshot_t;

esp_err_t interior_light_init(void);

/*
 * Maps the headlight Desired State onto the interior colour and records the
 * intent. Non-blocking: no scan, no connect, no GATT, no waiting. Latest state
 * wins, so rapid colour-picker updates collapse into a single pending value.
 */
void interior_light_follow_desired(const sp624e_desired_state_t *desired);

/* Direct intent, same non-blocking contract. */
void interior_light_set_color(uint8_t red, uint8_t green, uint8_t blue);
void interior_light_set_off(void);

/* Advances the state machine. Call from an existing runtime loop. */
void interior_light_service(void);

/*
 * Aborts an in-flight connect attempt so SP624E recovery can take the BLE
 * master role. An established interior link is left alone: it does not block
 * scanning or connecting. Registered with the connection manager guard.
 */
void interior_light_release_master(const char *reason);

bool interior_light_is_connected(void);
void interior_light_get_snapshot(interior_light_snapshot_t *snapshot);
const char *interior_light_state_name(interior_light_state_t state);
