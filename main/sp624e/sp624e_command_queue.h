#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SP624E_HOST_TEST
typedef struct { int unused; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
#define portMAX_DELAY 0
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage)
{
    return storage;
}
static inline int xSemaphoreTake(SemaphoreHandle_t lock, int timeout)
{
    (void)lock; (void)timeout; return 1;
}
static inline int xSemaphoreGive(SemaphoreHandle_t lock)
{
    (void)lock; return 1;
}
#else
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif
#include "sp624e_protocol.h"

#define SP624E_COMMAND_QUEUE_CAPACITY 16

typedef enum {
    SP624E_COMMAND_ENABLE_NOTIFICATIONS = 0,
    SP624E_COMMAND_STATE_QUERY,
    SP624E_COMMAND_EFFECT,
    SP624E_COMMAND_RGB,
    SP624E_COMMAND_BRIGHTNESS,
    SP624E_COMMAND_VERIFY,
    SP624E_COMMAND_RECONCILE,
    SP624E_COMMAND_RESTORE,
} sp624e_command_type_t;

typedef struct {
    uint32_t id;
    uint32_t generation;
    uint32_t session_id;
    sp624e_command_type_t type;
    uint8_t payload[SP624E_COMMAND_MAX_LEN];
    size_t payload_len;
    int retry_count;
    bool requires_verification;
} sp624e_command_t;

typedef struct {
    sp624e_command_t items[SP624E_COMMAND_QUEUE_CAPACITY];
    size_t count;
    uint32_t next_id;
    uint32_t coalesced_count;
    StaticSemaphore_t lock_storage;
    SemaphoreHandle_t lock;
} sp624e_command_queue_t;

void sp624e_command_queue_init(sp624e_command_queue_t *queue);
bool sp624e_command_queue_push(sp624e_command_queue_t *queue,
                               const sp624e_command_t *command,
                               bool allow_coalescing);
bool sp624e_command_queue_pop(sp624e_command_queue_t *queue,
                              uint32_t current_generation,
                              sp624e_command_t *command,
                              uint32_t *stale_discarded);
size_t sp624e_command_queue_discard_older(sp624e_command_queue_t *queue,
                                          uint32_t generation);
void sp624e_command_queue_clear(sp624e_command_queue_t *queue);
size_t sp624e_command_queue_depth(sp624e_command_queue_t *queue);
uint32_t sp624e_command_queue_coalesced_count(sp624e_command_queue_t *queue);
const char *sp624e_command_type_name(sp624e_command_type_t type);
