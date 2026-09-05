#include "rf_input.h"

#include <string.h>

rf_physical_channel_t rf_input_channel_from_mask(uint8_t mask)
{
    if (mask == 0 || (mask & (uint8_t)(mask - 1u)) != 0) {
        return RF_CHANNEL_INVALID;
    }
    for (uint8_t channel = 0; channel < RF_CHANNEL_COUNT; ++channel) {
        if (mask == (uint8_t)(1u << channel)) {
            return (rf_physical_channel_t)channel;
        }
    }
    return RF_CHANNEL_INVALID;
}

const char *rf_physical_channel_name(rf_physical_channel_t channel)
{
    switch (channel) {
    case RF_CHANNEL_D0: return "D0";
    case RF_CHANNEL_D1: return "D1";
    case RF_CHANNEL_D2: return "D2";
    case RF_CHANNEL_D3: return "D3";
    default: return "UNMAPPED";
    }
}

void rf_input_filter_init(rf_input_filter_t *filter, uint8_t initial_mask,
                          uint64_t now_ms)
{
    if (filter == NULL) return;
    memset(filter, 0, sizeof(*filter));
    filter->candidate_mask = initial_mask & 0x0fu;
    filter->stable_mask = filter->candidate_mask;
    filter->candidate_since_ms = now_ms;
    filter->press_latched = filter->stable_mask != 0;
}

bool rf_input_filter_sample(rf_input_filter_t *filter, uint8_t sampled_mask,
                            bool vt_active, uint64_t now_ms,
                            uint32_t debounce_ms, rf_input_event_t *event)
{
    if (filter == NULL || event == NULL) return false;
    sampled_mask &= 0x0fu;
    if (sampled_mask != filter->candidate_mask) {
        filter->candidate_mask = sampled_mask;
        filter->candidate_since_ms = now_ms;
        return false;
    }
    if (sampled_mask == filter->stable_mask ||
        now_ms - filter->candidate_since_ms < debounce_ms) return false;

    filter->stable_mask = sampled_mask;
    if (sampled_mask == 0) {
        filter->press_latched = false;
        return false;
    }
    if (filter->press_latched) return false;

    rf_physical_channel_t channel = rf_input_channel_from_mask(sampled_mask);
    if (channel == RF_CHANNEL_INVALID) return false;
    filter->press_latched = true;
    *event = (rf_input_event_t) {
        .channel = channel,
        .sampled_mask = sampled_mask,
        .vt_active = vt_active,
    };
    return true;
}
