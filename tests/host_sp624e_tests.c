#include <stdio.h>
#include <string.h>

#include "sp624e_mapping.h"
#include "sp624e_command_queue.h"
#include "sp624e_protocol.h"
#include "sp624e_state.h"
#include "sync/state_reconciler.h"
#include "sync/group_types.h"
#include "ble/ble_backoff.h"
#include "ble/ble_recovery_policy.h"
#include "animation/animation_player.h"
#include "animation/police_animation.h"
#include "indicator/indicator_policy.h"
#include "interior/interior_light_policy.h"
#include "diagnostics/ble_diag_format.h"
#include "diagnostics/system_health_policy.h"
#include "remote/rf_config.h"
#include "remote/rf_input.h"
#include "remote/remote_action.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void test_builders(void)
{
    uint8_t output[SP624E_COMMAND_MAX_LEN] = {0};
    size_t length = 0;
    const uint8_t query[] = {0x1d, 0x00};
    const uint8_t on[] = {0x0f, 0x01, 0x01};
    const uint8_t off[] = {0x0f, 0x01, 0x00};
    const uint8_t solid[] = {0x15, 0x01, 0x63};
    const uint8_t red[] = {0x13, 0x04, 0xff, 0x00, 0x00, 0x40};

    CHECK(sp624e_build_state_query(output, sizeof(output), &length) == 0);
    CHECK(length == sizeof(query) && memcmp(output, query, length) == 0);
    CHECK(sp624e_build_power(1, output, sizeof(output), &length) == 0);
    CHECK(length == sizeof(on) && memcmp(output, on, length) == 0);
    CHECK(sp624e_build_power(0, output, sizeof(output), &length) == 0);
    CHECK(length == sizeof(off) && memcmp(output, off, length) == 0);
    CHECK(sp624e_build_effect(0x63, output, sizeof(output), &length) == 0);
    CHECK(length == sizeof(solid) && memcmp(output, solid, length) == 0);
    CHECK(sp624e_build_rgb(255, 0, 0, 0x40, output, sizeof(output), &length) == 0);
    CHECK(length == sizeof(red) && memcmp(output, red, length) == 0);
    CHECK(sp624e_build_speed(0, output, sizeof(output), &length) == SP624E_PROTOCOL_INVALID_VALUE);
    CHECK(sp624e_build_speed(11, output, sizeof(output), &length) == SP624E_PROTOCOL_INVALID_VALUE);
    CHECK(sp624e_build_mode(3, output, sizeof(output), &length) == SP624E_PROTOCOL_INVALID_VALUE);
    CHECK(sp624e_build_rgb(256, 0, 0, 0, output, sizeof(output), &length) ==
          SP624E_PROTOCOL_INVALID_VALUE);
    CHECK(sp624e_build_state_query(output, 1, &length) == SP624E_PROTOCOL_BUFFER_TOO_SMALL);
}

static void test_reassembly(void)
{
    sp624e_reassembly_t reassembly = {0};
    const uint8_t *message = NULL;
    size_t length = 0;
    const uint8_t single[] = {1, 4, 4, 1, 2, 3, 4};
    CHECK(sp624e_reassembly_push(&reassembly, single, sizeof(single), &message, &length) == 0);
    CHECK(length == 4 && memcmp(message, single + 3, 4) == 0);

    const uint8_t first[] = {1, 6, 3, 1, 2, 3};
    const uint8_t second[] = {2, 3, 4, 5, 6};
    CHECK(sp624e_reassembly_push(&reassembly, first, sizeof(first), &message, &length) ==
          SP624E_PROTOCOL_INCOMPLETE);
    CHECK(sp624e_reassembly_push(&reassembly, second, sizeof(second), &message, &length) == 0);
    const uint8_t expected[] = {1, 2, 3, 4, 5, 6};
    CHECK(length == sizeof(expected) && memcmp(message, expected, length) == 0);

    CHECK(sp624e_reassembly_push(&reassembly, first, sizeof(first), &message, &length) ==
          SP624E_PROTOCOL_INCOMPLETE);
    const uint8_t out_of_order[] = {3, 1, 4};
    CHECK(sp624e_reassembly_push(&reassembly, out_of_order, sizeof(out_of_order),
                                 &message, &length) == SP624E_PROTOCOL_OUT_OF_SEQUENCE);

    CHECK(sp624e_reassembly_push(&reassembly, first, sizeof(first), &message, &length) ==
          SP624E_PROTOCOL_INCOMPLETE);
    CHECK(sp624e_reassembly_push(&reassembly, first, sizeof(first), &message, &length) ==
          SP624E_PROTOCOL_INCOMPLETE);
    const uint8_t duplicate[] = {2, 1, 4};
    CHECK(sp624e_reassembly_push(&reassembly, duplicate, sizeof(duplicate), &message, &length) ==
          SP624E_PROTOCOL_INCOMPLETE);
    CHECK(sp624e_reassembly_push(&reassembly, duplicate, sizeof(duplicate), &message, &length) ==
          SP624E_PROTOCOL_OUT_OF_SEQUENCE);

    const uint8_t oversized[] = {1, SP624E_MESSAGE_MAX_LEN + 1, 0};
    CHECK(sp624e_reassembly_push(&reassembly, oversized, sizeof(oversized), &message, &length) ==
          SP624E_PROTOCOL_OVERSIZED);
    const uint8_t truncated[] = {1, 4, 4, 1, 2};
    CHECK(sp624e_reassembly_push(&reassembly, truncated, sizeof(truncated), &message, &length) ==
          SP624E_PROTOCOL_INVALID_PACKET);
}

static void test_state(void)
{
    const sp624e_light_state_t invalid_a = {0};
    const sp624e_light_state_t invalid_b = {0};
    CHECK(sp624e_state_equal(&invalid_a, &invalid_b));

    const uint8_t payload[] = {
        1, 255, 10, 0, 0x65, 0, 0, 255, 255, 16,
        1, 3, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 1, 64, 0
    };
    sp624e_light_state_t state;
    CHECK(sp624e_state_parse(payload, sizeof(payload), 1234, &state) == 0);
    CHECK(state.valid && state.power && state.brightness == 255 && state.speed == 10);
    CHECK(state.effect == 0x65 && state.red == 0 && state.green == 255 && state.blue == 255);
    CHECK(state.input == 1 && state.white == 64 && state.reserved_last == 0);
    CHECK(sp624e_state_equal(&state, &state));
    CHECK(!sp624e_state_equal(&invalid_a, &state));
    CHECK(!sp624e_state_equal(NULL, &state));
    sp624e_light_state_t changed = state;
    changed.red++;
    CHECK(sp624e_state_diff(&state, &changed) == SP624E_STATE_DIFF_RGB);
    CHECK(sp624e_state_parse(payload, 12, 0, &state) == SP624E_PROTOCOL_INVALID_PACKET);
}

static void test_mapping(void)
{
    sp624e_mapping_t mapping = {
        .valid = true,
        .left = {.type = 0, .val = {1, 2, 3, 4, 5, 6}},
        .right = {.type = 1, .val = {6, 5, 4, 3, 2, 1}},
        .version = SP624E_MAPPING_VERSION,
    };
    uint8_t encoded[SP624E_MAPPING_ENCODED_LEN];
    size_t length = 0;
    CHECK(sp624e_mapping_encode(&mapping, encoded, sizeof(encoded), &length) == 0);
    CHECK(length == sizeof(encoded));
    sp624e_mapping_t decoded;
    CHECK(sp624e_mapping_decode(encoded, length, &decoded) == 0);
    CHECK(decoded.valid && decoded.version == mapping.version);
    CHECK(sp624e_address_equal(&decoded.left, &mapping.left));
    CHECK(sp624e_address_equal(&decoded.right, &mapping.right));
    CHECK(sp624e_mapping_decode(encoded, length - 1, &decoded) != 0);
    mapping.right = mapping.left;
    CHECK(sp624e_mapping_encode(&mapping, encoded, sizeof(encoded), &length) != 0);
}

static void test_reconciler(void)
{
    const uint8_t payload[] = {
        1, 64, 9, 0, 0x63, 0, 255, 0, 0, 16,
        1, 3, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 1, 255, 0
    };
    sp624e_light_state_t observed;
    CHECK(sp624e_state_parse(payload, sizeof(payload), 0, &observed) == 0);
    sp624e_desired_state_t desired = {
        .valid = true, .generation = 7, .power = true,
        .light_mode = SP624E_LIGHT_MODE_RGB,
        .effect = SP624E_EFFECT_SOLID, .mode = 0,
        .red = 255, .green = 0, .blue = 0, .brightness = 64,
        .white = 255, .speed = 9,
    };
    CHECK(sp624e_reconciler_matches(&desired, &observed));
    sp624e_reconcile_plan_t plan = sp624e_reconciler_plan(&desired, &observed);
    CHECK(plan.valid && plan.already_synchronized && plan.count == 0);

    desired.green = 255;
    plan = sp624e_reconciler_plan(&desired, &observed);
    CHECK(plan.valid && !plan.already_synchronized && plan.count == 1);
    CHECK(plan.commands[0].type == SP624E_RECONCILE_CMD_RGB);
    CHECK(plan.commands[0].red == 255 && plan.commands[0].green == 255 &&
          plan.commands[0].blue == 0 && plan.commands[0].level == 64);

    desired.green = 0;
    desired.brightness = 32;
    plan = sp624e_reconciler_plan(&desired, &observed);
    CHECK(plan.count == 1 && plan.commands[0].type == SP624E_RECONCILE_CMD_BRIGHTNESS);

    desired.light_mode = SP624E_LIGHT_MODE_WHITE;
    desired.effect = SP624E_EFFECT_WHITE;
    desired.white = 80;
    plan = sp624e_reconciler_plan(&desired, &observed);
    CHECK(plan.valid && !plan.unsupported_difference && plan.count == 2);
    CHECK(plan.commands[0].type == SP624E_RECONCILE_CMD_EFFECT_WHITE);
    CHECK(plan.commands[1].type == SP624E_RECONCILE_CMD_WHITE &&
          plan.commands[1].level == 80);

    sp624e_light_state_t observed_white = observed;
    observed_white.effect = SP624E_EFFECT_WHITE;
    observed_white.white = 80;
    CHECK(sp624e_reconciler_matches(&desired, &observed_white));
    plan = sp624e_reconciler_plan(&desired, &observed_white);
    CHECK(plan.already_synchronized && plan.count == 0);

    sp624e_desired_set_rgb(&desired, 1, 2, 3, 44);
    plan = sp624e_reconciler_plan(&desired, &observed_white);
    CHECK(plan.valid && plan.count == 2);
    CHECK(plan.commands[0].type == SP624E_RECONCILE_CMD_RGB);
    CHECK(plan.commands[0].red == 1 && plan.commands[0].green == 2 &&
          plan.commands[0].blue == 3 && plan.commands[0].level == 44);
    CHECK(plan.commands[1].type == SP624E_RECONCILE_CMD_EFFECT_SOLID);

    CHECK(desired.light_mode == SP624E_LIGHT_MODE_RGB &&
          desired.effect == SP624E_EFFECT_SOLID && desired.power);
    sp624e_desired_set_white(&desired, 123);
    CHECK(desired.light_mode == SP624E_LIGHT_MODE_WHITE &&
          desired.effect == SP624E_EFFECT_WHITE && desired.white == 123 &&
          desired.brightness == 123 && desired.power);

    sp624e_light_state_t equivalent = observed;
    equivalent.speed++;
    CHECK(sp624e_visual_equivalent(&observed, &equivalent));
    equivalent.white++;
    CHECK(sp624e_visual_equivalent(&observed, &equivalent));
    equivalent.blue++;
    CHECK(!sp624e_visual_equivalent(&observed, &equivalent));

    sp624e_light_state_t equivalent_white = observed_white;
    equivalent_white.red++;
    equivalent_white.brightness++;
    CHECK(sp624e_visual_equivalent(&observed_white, &equivalent_white));
    equivalent_white.white++;
    CHECK(!sp624e_visual_equivalent(&observed_white, &equivalent_white));
}

static void test_command_queue(void)
{
    sp624e_command_queue_t queue;
    sp624e_command_queue_init(&queue);
    sp624e_command_t first = {
        .generation = 10, .type = SP624E_COMMAND_RGB,
        .payload = {0x13, 0x04, 1, 2, 3, 4}, .payload_len = 6,
        .requires_verification = true,
    };
    sp624e_command_t newest = first;
    newest.generation = 11;
    newest.payload[2] = 9;
    CHECK(sp624e_command_queue_push(&queue, &first, true));
    CHECK(sp624e_command_queue_push(&queue, &newest, true));
    CHECK(sp624e_command_queue_depth(&queue) == 1);
    CHECK(sp624e_command_queue_coalesced_count(&queue) == 1);
    sp624e_command_t output;
    uint32_t stale = 0;
    CHECK(sp624e_command_queue_pop(&queue, 11, &output, &stale));
    CHECK(stale == 0 && output.generation == 11 && output.payload[2] == 9);

    sp624e_command_t old = {.generation = 20, .type = SP624E_COMMAND_EFFECT};
    sp624e_command_t current = {.generation = 21, .type = SP624E_COMMAND_BRIGHTNESS};
    CHECK(sp624e_command_queue_push(&queue, &old, false));
    CHECK(sp624e_command_queue_push(&queue, &current, false));
    CHECK(sp624e_command_queue_pop(&queue, 21, &output, &stale));
    CHECK(stale == 1 && output.generation == 21 &&
          output.type == SP624E_COMMAND_BRIGHTNESS);
    CHECK(!sp624e_command_queue_pop(&queue, 21, &output, &stale));

    sp624e_command_t stale_reconcile = {
        .generation = 30, .type = SP624E_COMMAND_RECONCILE,
    };
    sp624e_command_t zero_generation = {
        .generation = 0, .type = SP624E_COMMAND_STATE_QUERY,
    };
    CHECK(sp624e_command_queue_push(&queue, &stale_reconcile, false));
    CHECK(sp624e_command_queue_push(&queue, &zero_generation, false));
    CHECK(sp624e_command_queue_discard_older(&queue, 31) == 1);
    CHECK(sp624e_command_queue_depth(&queue) == 1);
    CHECK(sp624e_command_queue_pop(&queue, 31, &output, &stale));
    CHECK(output.type == SP624E_COMMAND_STATE_QUERY);

    CHECK(sp624e_command_queue_push(&queue, &current, false));
    sp624e_command_queue_clear(&queue);
    CHECK(sp624e_command_queue_depth(&queue) == 0);

    /* K: RED -> PURPLE -> WHITE while offline; only latest generation survives. */
    sp624e_command_t red = {.generation = 40, .type = SP624E_COMMAND_RECONCILE};
    sp624e_command_t purple = {.generation = 41, .type = SP624E_COMMAND_RECONCILE};
    sp624e_command_t white = {.generation = 42, .type = SP624E_COMMAND_RECONCILE};
    CHECK(sp624e_command_queue_push(&queue, &red, true));
    CHECK(sp624e_command_queue_push(&queue, &purple, true));
    CHECK(sp624e_command_queue_push(&queue, &white, true));
    CHECK(sp624e_command_queue_depth(&queue) == 1);
    CHECK(sp624e_command_queue_pop(&queue, 42, &output, &stale));
    CHECK(output.generation == 42);
}

static void test_backoff(void)
{
    CHECK(ble_backoff_base_ms(1) == 500);
    CHECK(ble_backoff_base_ms(2) == 1000);
    CHECK(ble_backoff_base_ms(3) == 2000);
    CHECK(ble_backoff_base_ms(4) == 4000);
    CHECK(ble_backoff_base_ms(5) == 8000);
    CHECK(ble_backoff_base_ms(6) == 10000);
    CHECK(ble_backoff_base_ms(99) == 10000);
    CHECK(ble_backoff_with_jitter(500, -10) == 450);
    CHECK(ble_backoff_with_jitter(500, 10) == 550);
    CHECK(ble_backoff_with_jitter(1000, -7) == 930);
    CHECK(ble_backoff_with_jitter(10000, 7) == 10700);
    CHECK(ble_backoff_absent_retry_ms() == 1500);
}

static void test_fast_recovery_policy(void)
{
    ble_disconnect_evidence_t stable_drop = {
        .stable_link = true,
        .platform_operational = true,
    };
    CHECK(ble_recovery_classify(&stable_drop) == BLE_DISCONNECT_LIKELY_POWER_CYCLE);
    ble_disconnect_evidence_t stable_without_platform = {.stable_link = true};
    CHECK(ble_recovery_classify(&stable_without_platform) == BLE_DISCONNECT_NORMAL);
    CHECK(strcmp(ble_disconnect_classification_name(
                     BLE_DISCONNECT_LIKELY_POWER_CYCLE),
                 "LIKELY_POWER_CYCLE") == 0);

    ble_recovery_window_t window;
    ble_recovery_window_start(&window, BLE_DISCONNECT_LIKELY_POWER_CYCLE,
                              1000, 5000);
    /* A-C/F/G: 100, 500, 1500 ms outages and ADV at 200/1000 ms remain fast. */
    CHECK(ble_recovery_window_is_fast(&window, 1100));
    CHECK(ble_recovery_window_is_fast(&window, 1500));
    CHECK(ble_recovery_window_is_fast(&window, 2500));
    CHECK(ble_recovery_window_is_fast(&window, 1200));
    CHECK(ble_recovery_window_is_fast(&window, 2000));

    /* D/E: simultaneous or 300 ms staggered peer loss strengthens classification. */
    ble_disconnect_evidence_t dual = {.peer_dropped_recently = true};
    CHECK(ble_recovery_classify(&dual) == BLE_DISCONNECT_LIKELY_POWER_CYCLE);

    /* H/I: bounded 0x3E retries stay short only inside phase 1. */
    CHECK(ble_recovery_retry_delay_ms(&window, 2200, 150, 500) == 150);
    CHECK(ble_recovery_retry_delay_ms(&window, 2350, 150, 500) == 150);

    /* J: peripheral absent for 30 s exits fast phase and returns normal delay. */
    CHECK(!ble_recovery_window_is_fast(&window, 6000));
    CHECK(ble_recovery_retry_delay_ms(&window, 31000, 150, 500) == 500);

    ble_disconnect_evidence_t establishment = {
        .connection_establishment_failure = true,
    };
    CHECK(ble_recovery_classify(&establishment) ==
          BLE_DISCONNECT_CONNECTION_ESTABLISHMENT_FAILURE);
    ble_disconnect_evidence_t manual = {.manual = true, .stable_link = true};
    CHECK(ble_recovery_classify(&manual) == BLE_DISCONNECT_MANUAL);
    ble_disconnect_evidence_t weak = {.weak_signal = true};
    CHECK(ble_recovery_classify(&weak) == BLE_DISCONNECT_LIKELY_RF_LOSS);
    ble_disconnect_evidence_t stable_weak = {
        .stable_link = true,
        .platform_operational = true,
        .weak_signal = true,
    };
    CHECK(ble_recovery_classify(&stable_weak) == BLE_DISCONNECT_LIKELY_RF_LOSS);
    ble_disconnect_evidence_t supervision_weak = {
        .supervision_timeout = true,
        .weak_signal = true,
    };
    CHECK(ble_recovery_classify(&supervision_weak) ==
          BLE_DISCONNECT_LIKELY_POWER_CYCLE);

    /* Duplicate-scan regression: same MAC after reboot must generate fresh ADV. */
    ble_recovery_scan_profile_t fast_scan = ble_recovery_scan_profile(true);
    CHECK(!fast_scan.passive);
    CHECK(!fast_scan.filter_duplicates);
    CHECK(fast_scan.window_units == fast_scan.interval_units);
    CHECK(fast_scan.duration_ms == 5000);
    ble_recovery_scan_profile_t normal_scan = ble_recovery_scan_profile(false);
    CHECK(normal_scan.passive);
    CHECK(normal_scan.filter_duplicates);
    CHECK(normal_scan.window_units < normal_scan.interval_units);

    /* Peer update: keep 1500 ms at normal RSSI; favor stability only when weak. */
    CHECK(ble_supervision_timeout_response(150, 400, -64, -88) == 150);
    CHECK(ble_supervision_timeout_response(150, 400, -76, -88) == 150);
    CHECK(ble_supervision_timeout_response(150, 400, -88, -88) == 400);
    CHECK(ble_supervision_timeout_response(150, 100, -90, -88) == 150);
}

static void test_group_sync_invariant(void)
{
    CHECK(sp624e_group_generation_is_synced(7, 7, 7, true));
    CHECK(!sp624e_group_generation_is_synced(7, 7, 7, false));
    CHECK(!sp624e_group_generation_is_synced(7, 7, 6, true));
    CHECK(!sp624e_group_generation_is_synced(0, 0, 0, true));
}

static void test_persistence_debounce(void)
{
    sp624e_desired_state_t desired = {.valid = true, .generation = 4};
    CHECK(sp624e_desired_needs_persistence(&desired, 3, false, false));
    CHECK(!sp624e_desired_needs_persistence(&desired, 4, false, false));
    CHECK(!sp624e_desired_needs_persistence(&desired, 3, true, false));
    CHECK(!sp624e_desired_needs_persistence(&desired, 3, false, true));
    desired.valid = false;
    CHECK(!sp624e_desired_needs_persistence(&desired, 3, false, false));
}

static void test_animation_math(void)
{
    CHECK(animation_ease(ANIMATION_EASING_LINEAR, 0.5f) == 0.5f);
    CHECK(animation_ease(ANIMATION_EASING_IN_OUT, 0.5f) == 0.5f);
    CHECK(animation_ease(ANIMATION_EASING_OUT, 0.5f) == 0.75f);
    CHECK(animation_lerp_u8(0, 255, 0.0f) == 0);
    CHECK(animation_lerp_u8(0, 255, 0.5f) == 128);
    CHECK(animation_lerp_u8(0, 255, 1.0f) == 255);
    CHECK(animation_lerp_u8(255, 0, 0.5f) == 128);
    CHECK(animation_scale_elapsed(0, 5000, 1800) == 0);
    CHECK(animation_scale_elapsed(2500, 5000, 1800) == 900);
    CHECK(animation_scale_elapsed(5000, 5000, 1800) == 1800);
    CHECK(animation_scale_elapsed(6000, 5000, 1800) == 1800);
    const animation_keyframe_t keys[] = {
        {ANIMATION_MODE_RGB, 0, 10, 20, 0, 0, ANIMATION_EASING_LINEAR},
        {ANIMATION_MODE_RGB, 255, 110, 220, 200, 1000, ANIMATION_EASING_LINEAR},
    };
    animation_frame_t frame;
    CHECK(animation_sample(keys, 2, 0, &frame) && frame.red == 0 && frame.brightness == 0);
    CHECK(animation_sample(keys, 2, 500, &frame) && frame.red == 128 &&
          frame.green == 60 && frame.blue == 120 && frame.brightness == 100);
    CHECK(animation_sample(keys, 2, 1000, &frame) && frame.red == 255 &&
          frame.brightness == 200);
}

static void test_forced_animation_restore_plan(void)
{
    /* L/M policy: disconnect-cancelled Police/Welcome restore persistent
     * Desired State through same verified group reconcile plan. */
    sp624e_desired_state_t desired = {
        .valid = true, .light_mode = SP624E_LIGHT_MODE_RGB,
        .red = 12, .green = 34, .blue = 56, .brightness = 78,
    };
    sp624e_reconcile_plan_t plan = sp624e_reconciler_force_plan(&desired);
    CHECK(plan.valid && !plan.already_synchronized && plan.count == 2);
    CHECK(plan.commands[0].type == SP624E_RECONCILE_CMD_RGB);
    CHECK(plan.commands[0].red == 12 && plan.commands[0].green == 34 &&
          plan.commands[0].blue == 56 && plan.commands[0].level == 78);
    CHECK(plan.commands[1].type == SP624E_RECONCILE_CMD_EFFECT_SOLID);
    desired.light_mode = SP624E_LIGHT_MODE_WHITE;
    desired.white = 91;
    plan = sp624e_reconciler_force_plan(&desired);
    CHECK(plan.valid && plan.count == 2);
    CHECK(plan.commands[0].type == SP624E_RECONCILE_CMD_EFFECT_WHITE);
    CHECK(plan.commands[1].type == SP624E_RECONCILE_CMD_WHITE &&
          plan.commands[1].level == 91);
}

static void test_rf_input_filter(void)
{
    rf_input_filter_t filter;
    rf_input_event_t event;
    rf_input_filter_init(&filter, 0, 0);
    CHECK(!rf_input_filter_sample(&filter, 1u << RF_CHANNEL_D2, true, 10, 50, &event));
    CHECK(!rf_input_filter_sample(&filter, 1u << RF_CHANNEL_D2, true, 59, 50, &event));
    CHECK(rf_input_filter_sample(&filter, 1u << RF_CHANNEL_D2, true, 60, 50, &event));
    CHECK(event.channel == RF_CHANNEL_D2 && event.vt_active);
    CHECK(!rf_input_filter_sample(&filter, 1u << RF_CHANNEL_D2, true, 2000, 50, &event));
    CHECK(!rf_input_filter_sample(&filter, 0, false, 2010, 50, &event));
    CHECK(!rf_input_filter_sample(&filter, 0, false, 2060, 50, &event));
    CHECK(!rf_input_filter_sample(&filter, 1u << RF_CHANNEL_D2, false, 2070, 50, &event));
    CHECK(rf_input_filter_sample(&filter, 1u << RF_CHANNEL_D2, false, 2120, 50, &event));
    CHECK(rf_input_channel_from_mask(0) == RF_CHANNEL_INVALID);
    CHECK(rf_input_channel_from_mask(3) == RF_CHANNEL_INVALID);
    CHECK(rf_input_channel_from_mask(1u << RF_CHANNEL_D3) == RF_CHANNEL_D3);
}

static void test_rf_config(void)
{
    rf_remote_config_t config;
    rf_config_defaults(&config);
    CHECK(config.button4.type == REMOTE_ACTION_FAVORITE);
    CHECK(rf_config_mapping_valid(config.channel_map, false));
    CHECK(!rf_config_mapping_valid(config.channel_map, true));
    CHECK(config.channel_map[RF_CHANNEL_D0] == REMOTE_BUTTON_INVALID);
    CHECK(config.channel_map[RF_CHANNEL_D1] == REMOTE_BUTTON_INVALID);
    CHECK(config.channel_map[RF_CHANNEL_D2] == REMOTE_BUTTON_INVALID);
    CHECK(config.channel_map[RF_CHANNEL_D3] == REMOTE_BUTTON_INVALID);
    CHECK(config.police_speed == POLICE_SPEED_FAST);
    remote_button_t mapping[RF_CHANNEL_COUNT] = {
        REMOTE_BUTTON_2, REMOTE_BUTTON_4, REMOTE_BUTTON_1, REMOTE_BUTTON_3,
    };
    memcpy(config.channel_map, mapping, sizeof(mapping));
    config.button4.type = REMOTE_ACTION_RGB;
    config.button4.red = 128;
    config.button4.green = 0;
    config.button4.blue = 255;
    config.button4.brightness = 64;
    config.police_speed = POLICE_SPEED_VERY_FAST;
    CHECK(rf_config_mapping_valid(config.channel_map, true));
    uint8_t encoded[RF_REMOTE_CONFIG_ENCODED_SIZE];
    CHECK(rf_config_encode(&config, encoded));
    rf_remote_config_t decoded;
    CHECK(rf_config_decode(encoded, sizeof(encoded), &decoded));
    CHECK(memcmp(decoded.channel_map, mapping, sizeof(mapping)) == 0);
    CHECK(decoded.button4.type == REMOTE_ACTION_RGB &&
          decoded.button4.red == 128 && decoded.button4.blue == 255 &&
          decoded.button4.brightness == 64);
    CHECK(decoded.police_speed == POLICE_SPEED_VERY_FAST);
    CHECK(rf_config_resolve_button(&decoded, RF_CHANNEL_D2, false) ==
          REMOTE_BUTTON_1);
    CHECK(rf_config_resolve_button(&decoded, RF_CHANNEL_D2, true) ==
          REMOTE_BUTTON_INVALID);
    mapping[3] = REMOTE_BUTTON_1;
    CHECK(!rf_config_mapping_valid(mapping, true));
    uint8_t version1[RF_REMOTE_CONFIG_V1_ENCODED_SIZE];
    memcpy(version1, encoded, 11);
    version1[0] = 1;
    version1[11] = 0xa5;
    CHECK(rf_config_decode(version1, sizeof(version1), &decoded));
    CHECK(decoded.police_speed == POLICE_SPEED_FAST);
    CHECK(decoded.button4.type == REMOTE_ACTION_RGB && decoded.button4.blue == 255);
    encoded[12] = 0;
    CHECK(!rf_config_decode(encoded, sizeof(encoded), &decoded));
}

static void test_police_timeline(void)
{
    animation_frame_t frame;
    uint8_t phase;
    CHECK(police_animation_cycle_ms(POLICE_SPEED_SLOW) == 1800);
    CHECK(police_animation_cycle_ms(POLICE_SPEED_NORMAL) == 1350);
    CHECK(police_animation_cycle_ms(POLICE_SPEED_FAST) == 1110);
    CHECK(police_animation_cycle_ms(POLICE_SPEED_VERY_FAST) == 900);
    CHECK(strcmp(police_speed_name(POLICE_SPEED_VERY_FAST), "very_fast") == 0);
    CHECK(police_speed_from_name("normal") == POLICE_SPEED_NORMAL);
    CHECK(police_speed_from_name("turbo") == POLICE_SPEED_COUNT);

    CHECK(police_animation_sample(0, POLICE_SPEED_FAST, &frame, &phase));
    CHECK(phase == 0 && frame.red == 255 && frame.blue == 0 &&
          frame.brightness == 255 && frame.transition_barrier);
    CHECK(police_animation_sample(220, POLICE_SPEED_FAST, &frame, &phase));
    CHECK(phase == 1 && frame.red == 0 && frame.green == 0 && frame.blue == 0 &&
          frame.brightness == 255);
    CHECK(police_animation_sample(370, POLICE_SPEED_FAST, &frame, &phase));
    CHECK(phase == 2 && frame.red == 255 && frame.blue == 0 &&
          frame.brightness == 255);
    CHECK(police_animation_sample(590, POLICE_SPEED_FAST, &frame, &phase));
    CHECK(phase == 3 && frame.red == 0 && frame.green == 0 && frame.blue == 0 &&
          frame.brightness == 255);
    CHECK(police_animation_sample(740, POLICE_SPEED_FAST, &frame, &phase));
    CHECK(phase == 4 && frame.red == 0 && frame.blue == 255 &&
          frame.brightness == 255);
    CHECK(police_animation_sample(960, POLICE_SPEED_FAST, &frame, &phase));
    CHECK(phase == 5 && frame.red == 0 && frame.green == 0 && frame.blue == 0 &&
          frame.brightness == 255);
    CHECK(police_animation_sample(1110, POLICE_SPEED_FAST, &frame, &phase));
    CHECK(phase == 0 && frame.red == 255);
    uint32_t transitions = 0;
    uint8_t prior = 0xff;
    for (uint32_t elapsed = 0; elapsed < 30000; elapsed += 20) {
        CHECK(police_animation_sample(elapsed, POLICE_SPEED_VERY_FAST, &frame, &phase));
        if (phase != prior) transitions++;
        prior = phase;
    }
    CHECK(transitions > 195 && transitions < 205);

    sp624e_command_queue_t queue;
    sp624e_command_queue_init(&queue);
    for (uint8_t value = 0; value < POLICE_PATTERN_PHASE_COUNT; value++) {
        sp624e_command_t command = {
            .session_id = 7, .type = SP624E_COMMAND_RGB,
            .payload = {value}, .payload_len = 1,
        };
        CHECK(sp624e_command_queue_push(&queue, &command, false));
    }
    CHECK(sp624e_command_queue_depth(&queue) == POLICE_PATTERN_PHASE_COUNT);
    CHECK(sp624e_command_queue_coalesced_count(&queue) == 0);
    for (uint8_t value = 0; value < POLICE_PATTERN_PHASE_COUNT; value++) {
        sp624e_command_t output;
        CHECK(sp624e_command_queue_pop(&queue, 0, &output, NULL));
        CHECK(output.payload[0] == value);
    }
}

static void test_remote_action_plans(void)
{
    remote_button4_config_t button4 = {
        .type = REMOTE_ACTION_RGB, .red = 120, .green = 10, .blue = 240,
        .brightness = 80,
    };
    remote_action_plan_t plan;
    CHECK(remote_action_plan(REMOTE_BUTTON_1, &button4, &plan));
    CHECK(plan.type == REMOTE_INTENT_WHITE && plan.brightness == 255);
    CHECK(remote_action_plan(REMOTE_BUTTON_2, &button4, &plan));
    CHECK(plan.type == REMOTE_INTENT_RGB && plan.red == 255 &&
          plan.green == 0 && plan.blue == 0 && plan.brightness == 255);
    CHECK(remote_action_plan(REMOTE_BUTTON_3, &button4, &plan));
    CHECK(plan.type == REMOTE_INTENT_POLICE_TOGGLE);
    CHECK(remote_action_plan(REMOTE_BUTTON_4, &button4, &plan));
    CHECK(plan.type == REMOTE_INTENT_RGB && plan.red == 120 &&
          plan.green == 10 && plan.blue == 240 && plan.brightness == 80);
    button4.type = REMOTE_ACTION_FAVORITE;
    CHECK(remote_action_plan(REMOTE_BUTTON_4, &button4, &plan));
    CHECK(plan.type == REMOTE_INTENT_FAVORITE);
    button4.type = REMOTE_ACTION_POLICE;
    CHECK(remote_action_plan(REMOTE_BUTTON_4, &button4, &plan));
    CHECK(plan.type == REMOTE_INTENT_POLICE_TOGGLE);

    CHECK(!remote_action_can_execute(&plan, false, false, false, true));
    CHECK(!remote_action_can_execute(&plan, true, false, false, true));
    CHECK(!remote_action_can_execute(&plan, true, true, false, true));
    CHECK(remote_action_can_execute(&plan, true, true, true, true));
    plan.type = REMOTE_INTENT_WHITE;
    CHECK(!remote_action_can_execute(&plan, true, true, true, false));
    CHECK(remote_action_can_execute(&plan, true, false, false, true));
    CHECK(remote_action_can_execute(&plan, true, true, false, true));
    plan.type = REMOTE_INTENT_RGB;
    CHECK(remote_action_can_execute(&plan, true, false, false, false));
    CHECK(remote_action_can_execute(&plan, true, true, false, false));
}

static void test_indicator_policy(void)
{
    sp624e_desired_state_t desired = {
        .valid = true, .power = true, .light_mode = SP624E_LIGHT_MODE_WHITE,
    };
    sp624e_light_state_t white = {
        .valid = true, .power = true, .effect = SP624E_EFFECT_WHITE,
    };
    sp624e_light_state_t red = {
        .valid = true, .power = true, .effect = SP624E_EFFECT_SOLID,
        .red = 255,
    };
    indicator_decision_t decision = indicator_policy_evaluate(
        true, false, true, SP624E_GROUP_SYNCED, &desired, &white, &white);
    CHECK(!decision.on && decision.reason == INDICATOR_REASON_CONFIRMED_WHITE);
    decision = indicator_policy_evaluate(
        false, false, true, SP624E_GROUP_SYNCED, &desired, &red, &red);
    CHECK(decision.on && decision.reason == INDICATOR_REASON_CONFIRMED_SPECIAL);
    decision = indicator_policy_evaluate(
        false, true, true, SP624E_GROUP_DEGRADED, &desired, NULL, NULL);
    CHECK(decision.on && decision.reason == INDICATOR_REASON_ANIMATION);
    decision = indicator_policy_evaluate(
        true, true, false, SP624E_GROUP_DEGRADED, &desired, NULL, NULL);
    CHECK(!decision.on && decision.reason == INDICATOR_REASON_UNKNOWN);
    decision = indicator_policy_evaluate(
        true, false, false, SP624E_GROUP_DEGRADED, &desired, NULL, NULL);
    CHECK(!decision.on && decision.reason == INDICATOR_REASON_UNKNOWN);
    desired.light_mode = SP624E_LIGHT_MODE_RGB;
    decision = indicator_policy_evaluate(
        false, false, true, SP624E_GROUP_RECONCILING, &desired, &white, &white);
    CHECK(decision.on && decision.reason == INDICATOR_REASON_APPLYING_SPECIAL);
}

static void test_system_health_policy(void)
{
    CHECK(system_health_heartbeat_stale(0, 20000, 2000));
    CHECK(!system_health_heartbeat_stale(19000, 20000, 2000));
    CHECK(!system_health_heartbeat_stale(18000, 20000, 2000));
    CHECK(system_health_heartbeat_stale(17999, 20000, 2000));
    CHECK(!system_health_restart_due(0, 20000, 8000));
    CHECK(!system_health_restart_due(12000, 19999, 8000));
    CHECK(system_health_restart_due(12000, 20000, 8000));
}

static void test_ble_diag_hex(void)
{
    uint8_t bytes[8];
    size_t written = 0;
    CHECK(ble_diag_hex_decode("7E000503FF0000EF", bytes, sizeof(bytes), &written) ==
          BLE_DIAG_HEX_OK);
    CHECK(written == 8);
    CHECK(bytes[0] == 0x7E && bytes[3] == 0x03 && bytes[7] == 0xEF);

    memset(bytes, 0, sizeof(bytes));
    CHECK(ble_diag_hex_decode("0x7e 00:05-03", bytes, sizeof(bytes), &written) ==
          BLE_DIAG_HEX_OK);
    CHECK(written == 4 && bytes[0] == 0x7E && bytes[2] == 0x05);

    CHECK(ble_diag_hex_decode("7E0", bytes, sizeof(bytes), &written) ==
          BLE_DIAG_HEX_ODD_LENGTH);
    CHECK(ble_diag_hex_decode("7E 0 05", bytes, sizeof(bytes), &written) ==
          BLE_DIAG_HEX_ODD_LENGTH);
    CHECK(ble_diag_hex_decode("7EZZ", bytes, sizeof(bytes), &written) ==
          BLE_DIAG_HEX_INVALID_CHAR);
    CHECK(ble_diag_hex_decode("", bytes, sizeof(bytes), &written) == BLE_DIAG_HEX_EMPTY);
    CHECK(ble_diag_hex_decode("0x", bytes, sizeof(bytes), &written) == BLE_DIAG_HEX_EMPTY);
    CHECK(ble_diag_hex_decode("00112233445566778899", bytes, sizeof(bytes), &written) ==
          BLE_DIAG_HEX_TOO_LONG);
    /* A rejected payload never reports a length. */
    CHECK(written == 0);

    char text[32];
    const uint8_t payload[4] = {0x00, 0xFF, 0x41, 0x7F};
    CHECK(ble_diag_hex_encode(payload, sizeof(payload), text, sizeof(text)) == 4);
    CHECK(strcmp(text, "00 FF 41 7F") == 0);
    /* Truncation never overflows the destination. */
    char small[6];
    CHECK(ble_diag_hex_encode(payload, sizeof(payload), small, sizeof(small)) == 2);
    CHECK(strcmp(small, "00 FF") == 0);

    char ascii[8];
    CHECK(ble_diag_ascii_encode(payload, sizeof(payload), ascii, sizeof(ascii)) == 4);
    CHECK(strcmp(ascii, "..A.") == 0);
}

static void test_ble_diag_address_and_properties(void)
{
    uint8_t address[6];
    CHECK(ble_diag_address_parse("FF:FF:11:CD:AC:FA", address) == 0);
    /* NimBLE keeps the least significant byte first. */
    CHECK(address[0] == 0xFA && address[5] == 0xFF);
    char text[18];
    ble_diag_address_format(address, text, sizeof(text));
    CHECK(strcmp(text, "FF:FF:11:CD:AC:FA") == 0);
    CHECK(ble_diag_address_parse("FFFF11CDACFA", address) == 0);
    CHECK(address[0] == 0xFA);
    CHECK(ble_diag_address_parse("FF:FF:11:CD:AC", address) != 0);
    CHECK(ble_diag_address_parse("FF:FF:11:CD:AC:FA:00", address) != 0);
    CHECK(ble_diag_address_parse("not-an-address", address) != 0);

    char properties[64];
    ble_diag_properties_string(0x1C, properties, sizeof(properties));
    CHECK(strcmp(properties, "WRITE_NO_RESPONSE|WRITE|NOTIFY") == 0);
    ble_diag_properties_string(0x00, properties, sizeof(properties));
    CHECK(strcmp(properties, "NONE") == 0);
    CHECK(ble_diag_properties_are_interesting(BLE_DIAG_PROP_NOTIFY));
    CHECK(ble_diag_properties_are_interesting(BLE_DIAG_PROP_WRITE_NO_RSP));
    CHECK(ble_diag_properties_are_interesting(BLE_DIAG_PROP_INDICATE));
    CHECK(!ble_diag_properties_are_interesting(BLE_DIAG_PROP_READ));
    CHECK(!ble_diag_properties_are_interesting(BLE_DIAG_PROP_BROADCAST));
}

static void test_interior_light_mapping(void)
{
    sp624e_desired_state_t desired;
    memset(&desired, 0, sizeof(desired));

    /* Nothing asked for yet: interior stays dark. */
    interior_rgb_t rgb = interior_light_map_desired(&desired);
    CHECK(rgb.red == 0 && rgb.green == 0 && rgb.blue == 0);
    CHECK(interior_light_map_desired(NULL).red == 0);

    /* Default white is identified by light_mode, not by RGB bytes: even with
       a colour still loaded in the struct the interior must stay dark. */
    desired.valid = true;
    desired.power = true;
    desired.light_mode = SP624E_LIGHT_MODE_WHITE;
    desired.red = 255;
    desired.green = 0;
    desired.blue = 0;
    rgb = interior_light_map_desired(&desired);
    CHECK(rgb.red == 0 && rgb.green == 0 && rgb.blue == 0);

    /* Powered off also means dark. */
    desired.light_mode = SP624E_LIGHT_MODE_RGB;
    desired.power = false;
    rgb = interior_light_map_desired(&desired);
    CHECK(rgb.red == 0 && rgb.green == 0 && rgb.blue == 0);

    /* Active RGB is mirrored verbatim, brightness deliberately ignored. */
    desired.power = true;
    desired.brightness = 10;
    desired.red = 120;
    desired.green = 30;
    desired.blue = 220;
    rgb = interior_light_map_desired(&desired);
    CHECK(rgb.red == 120 && rgb.green == 30 && rgb.blue == 220);

    /* An RGB request that happens to be 255,255,255 is NOT default white. */
    desired.red = 255;
    desired.green = 255;
    desired.blue = 255;
    rgb = interior_light_map_desired(&desired);
    CHECK(rgb.red == 255 && rgb.green == 255 && rgb.blue == 255);

    CHECK(interior_rgb_differs((interior_rgb_t){1, 2, 3}, (interior_rgb_t){1, 2, 4}));
    CHECK(!interior_rgb_differs((interior_rgb_t){1, 2, 3}, (interior_rgb_t){1, 2, 3}));
}

static void test_interior_light_frame(void)
{
    uint8_t frame[INTERIOR_LIGHT_FRAME_LEN];
    interior_light_build_frame((interior_rgb_t){120, 30, 220}, frame);
    const uint8_t expected[INTERIOR_LIGHT_FRAME_LEN] = {
        0x7E, 0xFF, 0x05, 0x03, 0x78, 0x1E, 0xDC, 0xFF, 0xEF};
    CHECK(memcmp(frame, expected, sizeof(expected)) == 0);

    /* Visual off is the confirmed RGB command with all channels at zero; it is
       NOT a power-off command, which remains unknown. */
    interior_light_build_frame((interior_rgb_t){0, 0, 0}, frame);
    const uint8_t off[INTERIOR_LIGHT_FRAME_LEN] = {
        0x7E, 0xFF, 0x05, 0x03, 0x00, 0x00, 0x00, 0xFF, 0xEF};
    CHECK(memcmp(frame, off, sizeof(off)) == 0);

    interior_light_build_frame((interior_rgb_t){255, 0, 0}, frame);
    CHECK(frame[4] == 0xFF && frame[5] == 0x00 && frame[6] == 0x00);
    interior_light_build_frame((interior_rgb_t){0, 255, 0}, frame);
    CHECK(frame[4] == 0x00 && frame[5] == 0xFF && frame[6] == 0x00);
    interior_light_build_frame((interior_rgb_t){0, 0, 255}, frame);
    CHECK(frame[4] == 0x00 && frame[5] == 0x00 && frame[6] == 0xFF);
}

int main(void)
{
    test_builders();
    test_reassembly();
    test_state();
    test_mapping();
    test_reconciler();
    test_command_queue();
    test_backoff();
    test_fast_recovery_policy();
    test_group_sync_invariant();
    test_persistence_debounce();
    test_animation_math();
    test_forced_animation_restore_plan();
    test_rf_input_filter();
    test_rf_config();
    test_police_timeline();
    test_indicator_policy();
    test_remote_action_plans();
    test_system_health_policy();
    test_ble_diag_hex();
    test_ble_diag_address_and_properties();
    test_interior_light_mapping();
    test_interior_light_frame();
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    puts("SP624E host tests: PASS");
    return 0;
}
