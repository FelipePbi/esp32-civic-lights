#pragma once

#include <stdbool.h>

#include "esp_err.h"

/*
 * BLE Diagnostic / reverse engineering console.
 *
 * The module is a passive observer of a single, explicitly selected third
 * device. It never participates in SP624E identification, connection or
 * reconciliation, and it always yields the BLE master role back to the
 * connection manager (see ble_diagnostics_release_master).
 *
 * Diagnostic mode starts disabled: every command that touches the radio is
 * refused until `diag enable` runs.
 */

/* Registers the console commands and the connection manager master guard. */
esp_err_t ble_diagnostics_init(void);

/*
 * Aborts any diagnostic scan or connection attempt that currently holds the
 * BLE master role. Established diagnostic links are left alone because they do
 * not block scanning or connecting. Called by the connection manager right
 * before SP624E recovery needs the radio.
 */
void ble_diagnostics_release_master(const char *reason);

bool ble_diagnostics_is_enabled(void);
