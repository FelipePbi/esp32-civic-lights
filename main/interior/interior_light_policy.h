#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sync/desired_state.h"

/*
 * Pure mapping from the headlight Desired State to the interior colour.
 * Free of NimBLE and ESP-IDF so the host test suite can cover it.
 */

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} interior_rgb_t;

/*
 * Rule, expressed in the semantics the firmware already owns:
 *
 *   desired invalid            -> 0,0,0  (nothing is being asked for)
 *   desired power == false     -> 0,0,0
 *   light_mode == WHITE        -> 0,0,0  (default/OEM white: interior dark)
 *   otherwise                  -> desired red/green/blue verbatim
 *
 * `light_mode` is the explicit representation the project already uses for
 * "default white" (see indicator_policy, which turns the physical LED off on
 * exactly this condition). Comparing raw RGB against 255,255,255 would be a
 * weaker inference and is deliberately not used.
 *
 * Brightness is NOT folded into the channels: the LEDCAR brightness command is
 * unknown, and scaling RGB locally would silently invent a behaviour the
 * hardware evidence does not support.
 */
interior_rgb_t interior_light_map_desired(const sp624e_desired_state_t *desired);

/* True when the two colours differ in any channel. */
bool interior_rgb_differs(interior_rgb_t left, interior_rgb_t right);

/* Builds the 9-byte confirmed RGB frame: 7E FF 05 03 RR GG BB FF EF. */
#define INTERIOR_LIGHT_FRAME_LEN 9
void interior_light_build_frame(interior_rgb_t rgb, uint8_t out[INTERIOR_LIGHT_FRAME_LEN]);
