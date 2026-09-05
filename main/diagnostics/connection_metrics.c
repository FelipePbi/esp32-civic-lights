#include "connection_metrics.h"

#include <string.h>

static sp624e_connection_metrics_t s_sides[SP624E_SIDE_COUNT];
static sp624e_group_metrics_t s_group;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;

void sp624e_metrics_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_sides, 0, sizeof(s_sides));
    memset(&s_group, 0, sizeof(s_group));
    xSemaphoreGive(s_lock);
}

void sp624e_metrics_get_side(sp624e_side_t side, sp624e_connection_metrics_t *metrics)
{
    if (side >= SP624E_SIDE_COUNT || metrics == NULL) return;
    if (s_lock == NULL) {
        memset(metrics, 0, sizeof(*metrics));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *metrics = s_sides[side];
    xSemaphoreGive(s_lock);
}

void sp624e_metrics_set_side(sp624e_side_t side, const sp624e_connection_metrics_t *metrics)
{
    if (side >= SP624E_SIDE_COUNT || metrics == NULL || s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_sides[side] = *metrics;
    xSemaphoreGive(s_lock);
}

void sp624e_metrics_get_group(sp624e_group_metrics_t *metrics)
{
    if (metrics == NULL) return;
    if (s_lock == NULL) {
        memset(metrics, 0, sizeof(*metrics));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *metrics = s_group;
    xSemaphoreGive(s_lock);
}

void sp624e_metrics_set_group(const sp624e_group_metrics_t *metrics)
{
    if (metrics == NULL || s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_group = *metrics;
    xSemaphoreGive(s_lock);
}
