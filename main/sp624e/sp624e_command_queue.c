#include "sp624e_command_queue.h"

#include <string.h>

static bool replaceable(sp624e_command_type_t type)
{
    return type == SP624E_COMMAND_EFFECT || type == SP624E_COMMAND_RGB ||
           type == SP624E_COMMAND_BRIGHTNESS || type == SP624E_COMMAND_RECONCILE;
}

void sp624e_command_queue_init(sp624e_command_queue_t *queue)
{
    memset(queue, 0, sizeof(*queue));
    queue->next_id = 1;
    queue->lock = xSemaphoreCreateMutexStatic(&queue->lock_storage);
}

bool sp624e_command_queue_push(sp624e_command_queue_t *queue,
                               const sp624e_command_t *command,
                               bool allow_coalescing)
{
    if (queue == NULL || command == NULL || command->payload_len > SP624E_COMMAND_MAX_LEN) {
        return false;
    }
    xSemaphoreTake(queue->lock, portMAX_DELAY);
    sp624e_command_t value = *command;
    value.id = queue->next_id++;
    if (queue->next_id == 0) queue->next_id = 1;
    if (allow_coalescing && replaceable(value.type)) {
        for (size_t i = queue->count; i > 0; --i) {
            sp624e_command_t *pending = &queue->items[i - 1];
            if (pending->type == value.type) {
                *pending = value;
                queue->coalesced_count++;
                xSemaphoreGive(queue->lock);
                return true;
            }
        }
    }
    if (queue->count >= SP624E_COMMAND_QUEUE_CAPACITY) {
        xSemaphoreGive(queue->lock);
        return false;
    }
    queue->items[queue->count++] = value;
    xSemaphoreGive(queue->lock);
    return true;
}

bool sp624e_command_queue_pop(sp624e_command_queue_t *queue,
                              uint32_t current_generation,
                              sp624e_command_t *command,
                              uint32_t *stale_discarded)
{
    if (queue == NULL || command == NULL) return false;
    uint32_t discarded = 0;
    xSemaphoreTake(queue->lock, portMAX_DELAY);
    while (queue->count > 0) {
        sp624e_command_t value = queue->items[0];
        memmove(&queue->items[0], &queue->items[1],
                (queue->count - 1) * sizeof(queue->items[0]));
        queue->count--;
        if (value.generation != 0 && current_generation != 0 &&
            value.generation < current_generation) {
            discarded++;
            continue;
        }
        *command = value;
        xSemaphoreGive(queue->lock);
        if (stale_discarded != NULL) *stale_discarded = discarded;
        return true;
    }
    xSemaphoreGive(queue->lock);
    if (stale_discarded != NULL) *stale_discarded = discarded;
    return false;
}

size_t sp624e_command_queue_discard_older(sp624e_command_queue_t *queue,
                                          uint32_t generation)
{
    if (queue == NULL) return 0;
    xSemaphoreTake(queue->lock, portMAX_DELAY);
    size_t output = 0;
    for (size_t i = 0; i < queue->count; ++i) {
        if (queue->items[i].generation != 0 && queue->items[i].generation < generation) {
            continue;
        }
        if (output != i) queue->items[output] = queue->items[i];
        output++;
    }
    size_t discarded = queue->count - output;
    queue->count = output;
    xSemaphoreGive(queue->lock);
    return discarded;
}

void sp624e_command_queue_clear(sp624e_command_queue_t *queue)
{
    if (queue == NULL) return;
    xSemaphoreTake(queue->lock, portMAX_DELAY);
    queue->count = 0;
    xSemaphoreGive(queue->lock);
}

size_t sp624e_command_queue_depth(sp624e_command_queue_t *queue)
{
    if (queue == NULL) return 0;
    xSemaphoreTake(queue->lock, portMAX_DELAY);
    size_t depth = queue->count;
    xSemaphoreGive(queue->lock);
    return depth;
}

uint32_t sp624e_command_queue_coalesced_count(sp624e_command_queue_t *queue)
{
    if (queue == NULL) return 0;
    xSemaphoreTake(queue->lock, portMAX_DELAY);
    uint32_t count = queue->coalesced_count;
    xSemaphoreGive(queue->lock);
    return count;
}

const char *sp624e_command_type_name(sp624e_command_type_t type)
{
    switch (type) {
    case SP624E_COMMAND_ENABLE_NOTIFICATIONS: return "ENABLE_NOTIFICATIONS";
    case SP624E_COMMAND_STATE_QUERY: return "STATE_QUERY";
    case SP624E_COMMAND_EFFECT: return "EFFECT";
    case SP624E_COMMAND_RGB: return "RGB";
    case SP624E_COMMAND_BRIGHTNESS: return "BRIGHTNESS";
    case SP624E_COMMAND_VERIFY: return "VERIFY";
    case SP624E_COMMAND_RECONCILE: return "RECONCILE";
    case SP624E_COMMAND_RESTORE: return "RESTORE";
    default: return "UNKNOWN";
    }
}
