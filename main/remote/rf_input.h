#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RF_CHANNEL_D0 = 0,
    RF_CHANNEL_D1,
    RF_CHANNEL_D2,
    RF_CHANNEL_D3,
    RF_CHANNEL_COUNT,
    RF_CHANNEL_INVALID = 0xff,
} rf_physical_channel_t;

typedef struct {
    uint8_t candidate_mask;
    uint8_t stable_mask;
    uint64_t candidate_since_ms;
    bool press_latched;
} rf_input_filter_t;

typedef struct {
    rf_physical_channel_t channel;
    uint8_t sampled_mask;
    bool vt_active;
} rf_input_event_t;

void rf_input_filter_init(rf_input_filter_t *filter, uint8_t initial_mask,
                          uint64_t now_ms);
bool rf_input_filter_sample(rf_input_filter_t *filter, uint8_t sampled_mask,
                            bool vt_active, uint64_t now_ms,
                            uint32_t debounce_ms, rf_input_event_t *event);
rf_physical_channel_t rf_input_channel_from_mask(uint8_t mask);
const char *rf_physical_channel_name(rf_physical_channel_t channel);
