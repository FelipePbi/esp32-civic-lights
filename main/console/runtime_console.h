#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/*
 * Shared UART0 line console.
 *
 * The reader task starts during boot so console access never depends on the
 * SP624E pipeline being up. Handlers claim lines in two passes: specific
 * handlers first, then the single fallback handler. The line buffer belongs to
 * the console task and is only valid during the call.
 */

#define RUNTIME_CONSOLE_LINE_MAX 128

/* Returns true when the handler consumed the line. */
typedef bool (*runtime_console_handler_t)(char *line, void *context);

esp_err_t runtime_console_start(void);

/*
 * Registers a handler. A fallback handler receives every line no specific
 * handler claimed; only one fallback may be registered.
 */
esp_err_t runtime_console_register(runtime_console_handler_t handler, void *context,
                                   bool fallback);

/* Command word comparison helper shared by console handlers. */
bool runtime_console_command_is(const char *token, const char *name);
