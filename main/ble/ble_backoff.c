#include "ble_backoff.h"

uint32_t ble_backoff_base_ms(uint32_t attempt)
{
    static const uint32_t values[] = {500, 1000, 2000, 4000, 8000};
    if (attempt == 0) attempt = 1;
    return attempt <= 5 ? values[attempt - 1] : 10000;
}

uint32_t ble_backoff_with_jitter(uint32_t base_ms, int jitter_percent)
{
    if (jitter_percent < -10) jitter_percent = -10;
    if (jitter_percent > 10) jitter_percent = 10;
    int32_t adjustment = ((int32_t)base_ms * (int32_t)jitter_percent) / 100;
    int32_t result = (int32_t)base_ms + adjustment;
    return result < 100 ? 100 : (uint32_t)result;
}

uint32_t ble_backoff_absent_retry_ms(void)
{
    return 1500;
}
