#include "interior_light_policy.h"

interior_rgb_t interior_light_map_desired(const sp624e_desired_state_t *desired)
{
    const interior_rgb_t off = {0, 0, 0};
    if (desired == NULL || !desired->valid || !desired->power) {
        return off;
    }
    if (desired->light_mode == SP624E_LIGHT_MODE_WHITE) {
        return off;
    }
    return (interior_rgb_t){desired->red, desired->green, desired->blue};
}

bool interior_rgb_differs(interior_rgb_t left, interior_rgb_t right)
{
    return left.red != right.red || left.green != right.green || left.blue != right.blue;
}

void interior_light_build_frame(interior_rgb_t rgb, uint8_t out[INTERIOR_LIGHT_FRAME_LEN])
{
    if (out == NULL) return;
    /* Confirmed visually on the car: 7E FF 05 03 RR GG BB FF EF. No checksum. */
    out[0] = 0x7E;
    out[1] = 0xFF;
    out[2] = 0x05;
    out[3] = 0x03;
    out[4] = rgb.red;
    out[5] = rgb.green;
    out[6] = rgb.blue;
    out[7] = 0xFF;
    out[8] = 0xEF;
}
