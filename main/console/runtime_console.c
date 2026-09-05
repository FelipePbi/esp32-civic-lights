#include "runtime_console.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define RUNTIME_CONSOLE_MAX_HANDLERS 4

static const char *TAG = "CONSOLE";

typedef struct {
    runtime_console_handler_t handler;
    void *context;
} console_entry_t;

static console_entry_t s_handlers[RUNTIME_CONSOLE_MAX_HANDLERS];
static size_t s_handler_count;
static console_entry_t s_fallback;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_started;

bool runtime_console_command_is(const char *token, const char *name)
{
    return token != NULL && name != NULL && strcasecmp(token, name) == 0;
}

static void dispatch(char *line)
{
    console_entry_t handlers[RUNTIME_CONSOLE_MAX_HANDLERS];
    console_entry_t fallback;
    size_t count;
    portENTER_CRITICAL(&s_mux);
    count = s_handler_count;
    memcpy(handlers, s_handlers, sizeof(handlers));
    fallback = s_fallback;
    portEXIT_CRITICAL(&s_mux);

    char scratch[RUNTIME_CONSOLE_LINE_MAX];
    for (size_t i = 0; i < count; ++i) {
        /* Handlers tokenize in place, so every candidate gets its own copy. */
        snprintf(scratch, sizeof(scratch), "%s", line);
        if (handlers[i].handler(scratch, handlers[i].context)) {
            return;
        }
    }
    if (fallback.handler != NULL) {
        snprintf(scratch, sizeof(scratch), "%s", line);
        (void)fallback.handler(scratch, fallback.context);
        return;
    }
    ESP_LOGW(TAG, "No handler for console line: %s", line);
}

static void console_task(void *arg)
{
    (void)arg;
    char line[RUNTIME_CONSOLE_LINE_MAX];
    size_t used = 0;
    while (true) {
        uint8_t byte;
        if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &byte, 1, portMAX_DELAY) != 1) continue;
        if (byte == '\r' || byte == '\n') {
            if (used > 0) {
                line[used] = '\0';
                used = 0;
                dispatch(line);
            }
        } else if (byte == 8 || byte == 127) {
            if (used > 0) used--;
        } else if (isprint(byte) && used + 1 < sizeof(line)) {
            line[used++] = (char)byte;
        }
    }
}

esp_err_t runtime_console_start(void)
{
    if (s_started) return ESP_OK;
    esp_err_t err = uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 512, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    if (xTaskCreate(console_task, "runtime_console", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "Runtime serial console ready on UART%d", CONFIG_ESP_CONSOLE_UART_NUM);
    return ESP_OK;
}

esp_err_t runtime_console_register(runtime_console_handler_t handler, void *context,
                                   bool fallback)
{
    if (handler == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t result = ESP_OK;
    portENTER_CRITICAL(&s_mux);
    if (fallback) {
        if (s_fallback.handler != NULL) {
            result = ESP_ERR_INVALID_STATE;
        } else {
            s_fallback.handler = handler;
            s_fallback.context = context;
        }
    } else if (s_handler_count >= RUNTIME_CONSOLE_MAX_HANDLERS) {
        result = ESP_ERR_NO_MEM;
    } else {
        s_handlers[s_handler_count].handler = handler;
        s_handlers[s_handler_count].context = context;
        s_handler_count++;
    }
    portEXIT_CRITICAL(&s_mux);
    return result;
}
