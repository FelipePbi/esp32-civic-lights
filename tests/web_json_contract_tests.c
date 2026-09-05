#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "animation/runtime_animation.h"
#include "cJSON.h"
#include "indicator/indicator.h"
#include "presets/preset_manager.h"
#include "remote/remote_controller.h"
#include "sp624e/sp624e_controller.h"
#include "sp624e/sp624e_protocol.h"
#include "web/json_codec.h"
#include "diagnostics/system_health.h"

static int failures;
static sp624e_group_snapshot_t snapshot;
static sp624e_favorite_preset_t favorite;
static runtime_animation_snapshot_t police;
static indicator_snapshot_t indicator;
static remote_controller_snapshot_t remote;
static system_health_snapshot_t system_health;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

int64_t esp_timer_get_time(void)
{
    return 1234567000;
}

uint8_t wifi_ap_client_count(void)
{
    return 2;
}

void sp624e_group_get_snapshot(sp624e_group_snapshot_t *output)
{
    if (output != NULL) *output = snapshot;
}

void preset_manager_get_favorite(sp624e_favorite_preset_t *output)
{
    if (output != NULL) *output = favorite;
}

void runtime_animation_get_snapshot(runtime_animation_snapshot_t *output)
{
    if (output != NULL) *output = police;
}

const char *runtime_animation_state_name(runtime_animation_state_t state)
{
    return state == RUNTIME_ANIMATION_RUNNING ? "running" : "idle";
}

void indicator_get_snapshot(indicator_snapshot_t *output)
{
    if (output != NULL) *output = indicator;
}

const char *indicator_reason_name(indicator_reason_t reason)
{
    return reason == INDICATOR_REASON_CONFIRMED_SPECIAL ?
           "CONFIRMED_SPECIAL" : "CONFIRMED_WHITE";
}

void remote_controller_get_snapshot(remote_controller_snapshot_t *output)
{
    if (output != NULL) *output = remote;
}

void system_health_get_snapshot(system_health_snapshot_t *output)
{
    if (output != NULL) *output = system_health;
}

const char *remote_action_type_name(remote_action_type_t type)
{
    return type == REMOTE_ACTION_RGB ? "rgb" : "favorite";
}

const char *rf_physical_channel_name(rf_physical_channel_t channel)
{
    return channel == RF_CHANNEL_D2 ? "D2" : "UNMAPPED";
}

const char *ble_connection_state_name(ble_connection_state_t state)
{
    switch (state) {
    case BLE_CONNECTION_UNKNOWN: return "UNKNOWN";
    case BLE_CONNECTION_DISCONNECTED: return "DISCONNECTED";
    case BLE_CONNECTION_WAITING_FOR_ADV: return "WAITING_FOR_ADV";
    case BLE_CONNECTION_CONNECTING: return "CONNECTING";
    case BLE_CONNECTION_CONNECTED: return "CONNECTED";
    case BLE_CONNECTION_DISCOVERING: return "DISCOVERING";
    case BLE_CONNECTION_SUBSCRIBING: return "SUBSCRIBING";
    case BLE_CONNECTION_QUERYING_STATE: return "QUERYING_STATE";
    case BLE_CONNECTION_SYNC_PENDING: return "SYNC_PENDING";
    case BLE_CONNECTION_RECONCILING: return "RECONCILING";
    case BLE_CONNECTION_READY: return "READY";
    case BLE_CONNECTION_BACKOFF: return "BACKOFF";
    case BLE_CONNECTION_FAST_RECOVERY: return "FAST_RECOVERY";
    case BLE_CONNECTION_RECOVERING: return "RECOVERING";
    case BLE_CONNECTION_ERROR: return "ERROR";
    default: return "INVALID";
    }
}

static cJSON *member(cJSON *object, const char *name)
{
    return object == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(object, name);
}

static bool bool_is(cJSON *object, const char *name, bool expected)
{
    cJSON *item = member(object, name);
    return expected ? cJSON_IsTrue(item) : cJSON_IsFalse(item);
}

static bool number_is(cJSON *object, const char *name, int expected)
{
    cJSON *item = member(object, name);
    return cJSON_IsNumber(item) && item->valueint == expected;
}

static bool string_is(cJSON *object, const char *name, const char *expected)
{
    cJSON *item = member(object, name);
    return cJSON_IsString(item) && item->valuestring != NULL &&
           strcmp(item->valuestring, expected) == 0;
}

static cJSON *parse_output(char *text)
{
    CHECK(text != NULL);
    if (text == NULL) return NULL;
    cJSON *root = cJSON_Parse(text);
    cJSON_free(text);
    CHECK(cJSON_IsObject(root));
    return root;
}

static void seed_rgb_snapshot(void)
{
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.controller_started = true;
    snapshot.white_available = true;
    snapshot.group_state = SP624E_GROUP_SYNCED;
    snapshot.desired = (sp624e_desired_state_t){
        .valid = true,
        .generation = 77,
        .power = true,
        .light_mode = SP624E_LIGHT_MODE_RGB,
        .effect = SP624E_EFFECT_SOLID,
        .red = 255,
        .green = 20,
        .blue = 20,
        .brightness = 64,
    };
    snapshot.sides[SP624E_SIDE_LEFT].connection = (ble_connection_manager_status_t){
        .state = BLE_CONNECTION_READY,
        .connected = true,
        .rssi = -71,
    };
    snapshot.sides[SP624E_SIDE_LEFT].metrics.reconnect_success_count = 3;
    snapshot.sides[SP624E_SIDE_LEFT].verified_generation = 77;
    snapshot.sides[SP624E_SIDE_LEFT].observed = (sp624e_light_state_t){
        .valid = true,
        .power = true,
        .effect = SP624E_EFFECT_SOLID,
        .red = 255,
        .green = 20,
        .blue = 20,
        .brightness = 64,
    };
    snapshot.sides[SP624E_SIDE_RIGHT].connection = (ble_connection_manager_status_t){
        .state = BLE_CONNECTION_BACKOFF,
        .connected = false,
        .rssi = -88,
    };
    snapshot.sides[SP624E_SIDE_RIGHT].metrics.reconnect_success_count = 5;
    snapshot.sides[SP624E_SIDE_RIGHT].metrics.last_disconnect_reason = 0x208;
    snapshot.sides[SP624E_SIDE_RIGHT].metrics.last_disconnect_timestamp_ms = 1230000;
    snapshot.sides[SP624E_SIDE_RIGHT].metrics.last_disconnect_classification =
        BLE_DISCONNECT_LIKELY_POWER_CYCLE;
    snapshot.sides[SP624E_SIDE_RIGHT].metrics.fast_recovery_status =
        BLE_FAST_RECOVERY_PASS;
    snapshot.sides[SP624E_SIDE_RIGHT].metrics.last_recovery_duration_ms = 1450;
    snapshot.sides[SP624E_SIDE_RIGHT].metrics.last_adv_after_loss_ms = 550;
    snapshot.sides[SP624E_SIDE_RIGHT].metrics.requested_supervision_timeout_ms = 1500;
    snapshot.sides[SP624E_SIDE_RIGHT].metrics.accepted_supervision_timeout_ms = 1500;
    snapshot.sides[SP624E_SIDE_RIGHT].verified_generation = 76;
    snapshot.sides[SP624E_SIDE_RIGHT].observed = (sp624e_light_state_t){
        .valid = true,
        .power = true,
        .effect = SP624E_EFFECT_SOLID,
        .red = 0,
        .green = 110,
        .blue = 255,
        .brightness = 91,
    };
    favorite = (sp624e_favorite_preset_t){
        .version = SP624E_FAVORITE_VERSION,
        .red = 145,
        .green = 28,
        .blue = 202,
        .brightness = 160,
    };
    memset(&remote, 0, sizeof(remote));
    remote.initialized = true;
    remote.has_last_button = true;
    remote.last_button = REMOTE_BUTTON_2;
    remote.last_channel = RF_CHANNEL_D2;
    remote.last_event_ms = 1234000;
    remote.last_action_accepted = true;
    remote.receiver.initialized = true;
    remote.receiver.mapping_complete = true;
    remote.receiver.discovery_active = false;
    remote.receiver.vt_active = true;
    remote.config.version = RF_REMOTE_CONFIG_VERSION;
    remote.config.channel_map[RF_CHANNEL_D0] = REMOTE_BUTTON_1;
    remote.config.channel_map[RF_CHANNEL_D1] = REMOTE_BUTTON_2;
    remote.config.channel_map[RF_CHANNEL_D2] = REMOTE_BUTTON_3;
    remote.config.channel_map[RF_CHANNEL_D3] = REMOTE_BUTTON_4;
    remote.config.button4.type = REMOTE_ACTION_RGB;
    remote.config.button4.red = 128;
    remote.config.button4.blue = 255;
    remote.config.button4.brightness = 64;
    remote.config.police_speed = POLICE_SPEED_VERY_FAST;
    police.state = RUNTIME_ANIMATION_RUNNING;
    police.elapsed_ms = 1200;
    indicator.initialized = true;
    indicator.on = true;
    indicator.reason = INDICATOR_REASON_CONFIRMED_SPECIAL;
    indicator.gpio_level = 1;
    indicator.last_change_ms = 1234500;
    system_health = (system_health_snapshot_t){
        .healthy = true,
        .supervisor_restart_count = 2,
        .free_heap = 123456,
        .minimum_free_heap = 65432,
        .connection_manager_heartbeat_ms = 1234501,
        .group_runtime_heartbeat_ms = 1234502,
        .rf_heartbeat_ms = 1234503,
        .indicator_heartbeat_ms = 1234504,
        .web_heartbeat_ms = 1234505,
        .ble_forced_recoveries = 4,
        .group_api_timeouts = 3,
        .rf_event_drops = 1,
        .indicator_gpio_level = 1,
        .indicator_last_change_ms = 1234500,
    };
    snprintf(system_health.reset_reason_name,
             sizeof(system_health.reset_reason_name), "SOFTWARE");
    snprintf(system_health.previous_recovery_reason,
             sizeof(system_health.previous_recovery_reason), "group_runtime_stalled");
}

static void check_rgb_observed(cJSON *observed)
{
    cJSON *left = member(observed, "left");
    cJSON *right = member(observed, "right");
    CHECK(cJSON_IsObject(left));
    CHECK(cJSON_IsObject(right));
    CHECK(bool_is(left, "valid", true));
    CHECK(string_is(left, "mode", "rgb"));
    CHECK(number_is(left, "r", 255));
    CHECK(number_is(left, "g", 20));
    CHECK(number_is(left, "b", 20));
    CHECK(number_is(left, "brightness", 64));
    CHECK(bool_is(right, "valid", true));
    CHECK(string_is(right, "mode", "rgb"));
    CHECK(number_is(right, "r", 0));
    CHECK(number_is(right, "g", 110));
    CHECK(number_is(right, "b", 255));
    CHECK(number_is(right, "brightness", 91));
}

static void test_status_contract(void)
{
    cJSON *root = parse_output(web_json_status());
    if (root == NULL) return;
    cJSON *group = member(root, "group");
    cJSON *left = member(root, "left");
    cJSON *right = member(root, "right");
    CHECK(string_is(root, "firmware", APP_FIRMWARE_VERSION));
    CHECK(number_is(root, "wifi_clients", 2));
    CHECK(string_is(group, "state", "SYNCED"));
    CHECK(bool_is(left, "connected", true));
    CHECK(bool_is(left, "ready", true));
    CHECK(string_is(left, "state", "READY"));
    CHECK(number_is(left, "rssi", -71));
    CHECK(bool_is(right, "connected", false));
    CHECK(bool_is(right, "ready", false));
    CHECK(string_is(right, "state", "BACKOFF"));
    CHECK(number_is(right, "rssi", -88));
    CHECK(number_is(right, "last_disconnect_reason", 0x208));
    CHECK(string_is(right, "disconnect_classification", "LIKELY_POWER_CYCLE"));
    CHECK(string_is(right, "fast_recovery", "PASS"));
    CHECK(number_is(right, "last_recovery_ms", 1450));
    CHECK(number_is(right, "last_adv_after_loss_ms", 550));
    CHECK(number_is(right, "supervision_timeout_accepted_ms", 1500));
    cJSON *remote_json = member(root, "remote");
    CHECK(bool_is(remote_json, "connected", true));
    CHECK(number_is(remote_json, "last_button", 2));
    CHECK(string_is(remote_json, "last_channel", "D2"));
    CHECK(string_is(member(remote_json, "button4"), "type", "rgb"));
    CHECK(string_is(member(remote_json, "police"), "state", "running"));
    CHECK(string_is(member(remote_json, "police"), "speed", "very_fast"));
    CHECK(bool_is(member(remote_json, "indicator"), "on", true));
    CHECK(number_is(member(remote_json, "indicator"), "gpio_level", 1));
    cJSON *health = member(root, "system_health");
    CHECK(bool_is(health, "healthy", true));
    CHECK(string_is(health, "reset_reason", "SOFTWARE"));
    CHECK(string_is(health, "previous_recovery_reason", "group_runtime_stalled"));
    CHECK(number_is(health, "supervisor_restarts", 2));
    CHECK(number_is(health, "free_heap", 123456));
    CHECK(number_is(member(health, "counters"), "ble_forced_recoveries", 4));
    CHECK(number_is(member(health, "counters"), "group_api_timeouts", 3));
    CHECK(number_is(member(health, "counters"), "rf_event_drops", 1));
    CHECK(number_is(member(health, "indicator"), "gpio_level", 1));
    CHECK(member(root, "welcome_animation") == NULL);
    check_rgb_observed(member(root, "observed"));
    cJSON_Delete(root);
}

static void test_state_contract(void)
{
    cJSON *root = parse_output(web_json_state());
    if (root == NULL) return;
    cJSON *verified = member(root, "verified_generation");
    check_rgb_observed(member(root, "observed"));
    CHECK(number_is(verified, "left", 77));
    CHECK(number_is(verified, "right", 76));
    cJSON_Delete(root);
}

static void test_websocket_snapshot_contract(void)
{
    cJSON *root = parse_output(web_json_snapshot_event("snapshot"));
    if (root == NULL) return;
    CHECK(string_is(root, "type", "snapshot"));
    CHECK(string_is(root, "group_state", "SYNCED"));
    CHECK(string_is(member(root, "left"), "state", "READY"));
    CHECK(string_is(member(root, "right"), "state", "BACKOFF"));
    CHECK(number_is(member(root, "remote"), "last_button", 2));
    CHECK(bool_is(member(root, "system_health"), "healthy", true));
    CHECK(member(root, "welcome_animation") == NULL);
    check_rgb_observed(member(root, "observed"));
    cJSON_Delete(root);
}

static void test_white_and_invalid_event_contract(void)
{
    snapshot.sides[SP624E_SIDE_LEFT].observed = (sp624e_light_state_t){
        .valid = true,
        .power = true,
        .brightness = 17,
        .effect = SP624E_EFFECT_WHITE,
        .red = 99,
        .green = 88,
        .blue = 77,
        .white = 123,
    };
    memset(&snapshot.sides[SP624E_SIDE_RIGHT].observed, 0,
           sizeof(snapshot.sides[SP624E_SIDE_RIGHT].observed));
    cJSON *root = parse_output(web_json_snapshot_event("observed_state"));
    if (root == NULL) return;
    cJSON *observed = member(root, "observed");
    cJSON *left = member(observed, "left");
    cJSON *right = member(observed, "right");
    CHECK(string_is(root, "type", "observed_state"));
    CHECK(bool_is(left, "valid", true));
    CHECK(string_is(left, "mode", "white"));
    CHECK(number_is(left, "brightness", 17));
    CHECK(number_is(left, "white", 123));
    CHECK(bool_is(right, "valid", false));
    CHECK(member(right, "mode") == NULL);
    CHECK(member(right, "r") == NULL);
    CHECK(member(right, "brightness") == NULL);
    CHECK(member(right, "white") == NULL);
    cJSON_Delete(root);
}

int main(void)
{
    seed_rgb_snapshot();
    test_status_contract();
    test_state_contract();
    test_websocket_snapshot_contract();
    test_white_and_invalid_event_contract();
    if (failures != 0) {
        fprintf(stderr, "%d web JSON contract test(s) failed\n", failures);
        return 1;
    }
    puts("Web JSON serializer contract tests: PASS");
    return 0;
}
