#include "json_codec.h"

#include "app_config.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "presets/preset_manager.h"
#include "sp624e/sp624e_controller.h"
#include "sp624e/sp624e_protocol.h"
#include "wifi/wifi_ap.h"
#include "animation/runtime_animation.h"
#include "indicator/indicator.h"
#include "remote/remote_controller.h"
#include "diagnostics/system_health.h"
#include "ble/ble_recovery_policy.h"

static cJSON *desired_json(const sp624e_desired_state_t *state)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "valid", state->valid);
    cJSON_AddNumberToObject(json, "generation", state->generation);
    cJSON_AddStringToObject(json, "mode",
                            state->light_mode == SP624E_LIGHT_MODE_WHITE ? "white" : "rgb");
    cJSON_AddNumberToObject(json, "r", state->red);
    cJSON_AddNumberToObject(json, "g", state->green);
    cJSON_AddNumberToObject(json, "b", state->blue);
    cJSON_AddNumberToObject(json, "brightness", state->brightness);
    cJSON_AddNumberToObject(json, "white", state->white);
    return json;
}

static cJSON *observed_json(const sp624e_light_state_t *state)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "valid", state->valid);
    if (!state->valid) return json;
    cJSON_AddBoolToObject(json, "power", state->power);
    cJSON_AddStringToObject(json, "mode",
                            state->effect == SP624E_EFFECT_WHITE ? "white" : "rgb");
    cJSON_AddNumberToObject(json, "effect", state->effect);
    cJSON_AddNumberToObject(json, "r", state->red);
    cJSON_AddNumberToObject(json, "g", state->green);
    cJSON_AddNumberToObject(json, "b", state->blue);
    cJSON_AddNumberToObject(json, "brightness", state->brightness);
    cJSON_AddNumberToObject(json, "white", state->white);
    return json;
}

static cJSON *side_status_json(const sp624e_side_snapshot_t *side)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "connected", side->connection.connected);
    cJSON_AddBoolToObject(json, "ready", side->connection.state == BLE_CONNECTION_READY);
    cJSON_AddStringToObject(json, "state", ble_connection_state_name(side->connection.state));
    cJSON_AddNumberToObject(json, "rssi", side->connection.rssi);
    cJSON_AddNumberToObject(json, "reconnect_count", side->metrics.reconnect_success_count);
    cJSON_AddNumberToObject(json, "forced_recoveries", side->metrics.forced_recoveries);
    cJSON_AddNumberToObject(json, "last_disconnect_ms",
                            (double)side->metrics.last_disconnect_timestamp_ms);
    cJSON_AddNumberToObject(json, "last_disconnect_reason",
                            side->metrics.last_disconnect_reason);
    cJSON_AddStringToObject(json, "disconnect_classification",
        ble_disconnect_classification_name(side->metrics.last_disconnect_classification));
    cJSON_AddStringToObject(json, "fast_recovery",
        ble_fast_recovery_status_name(side->metrics.fast_recovery_status));
    cJSON_AddNumberToObject(json, "last_recovery_ms",
                            side->metrics.last_recovery_duration_ms);
    cJSON_AddNumberToObject(json, "last_adv_after_loss_ms",
                            side->metrics.last_adv_after_loss_ms);
    cJSON_AddNumberToObject(json, "supervision_timeout_requested_ms",
                            side->metrics.requested_supervision_timeout_ms);
    cJSON_AddNumberToObject(json, "supervision_timeout_accepted_ms",
                            side->metrics.accepted_supervision_timeout_ms);
    cJSON_AddNumberToObject(json, "power_cycle_suspected_count",
                            side->metrics.power_cycle_suspected_count);
    cJSON_AddNumberToObject(json, "connection_0x3e_count",
                            side->metrics.connection_0x3e_count);
    cJSON_AddNumberToObject(json, "avg_adv_to_ready_ms",
                            side->metrics.avg_adv_to_ready_ms);
    cJSON_AddNumberToObject(json, "max_adv_to_ready_ms",
                            side->metrics.max_adv_to_ready_ms);
    cJSON_AddNumberToObject(json, "verified_generation", side->verified_generation);
    return json;
}

static cJSON *favorite_json(void)
{
    sp624e_favorite_preset_t preset;
    preset_manager_get_favorite(&preset);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mode", "rgb");
    cJSON_AddNumberToObject(json, "r", preset.red);
    cJSON_AddNumberToObject(json, "g", preset.green);
    cJSON_AddNumberToObject(json, "b", preset.blue);
    cJSON_AddNumberToObject(json, "brightness", preset.brightness);
    return json;
}

static cJSON *button4_json(const remote_button4_config_t *config)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", remote_action_type_name(config->type));
    cJSON_AddNumberToObject(json, "r", config->red);
    cJSON_AddNumberToObject(json, "g", config->green);
    cJSON_AddNumberToObject(json, "b", config->blue);
    cJSON_AddNumberToObject(json, "brightness", config->brightness);
    return json;
}

static cJSON *remote_json(void)
{
    remote_controller_snapshot_t remote = {0};
    runtime_animation_snapshot_t police = {0};
    indicator_snapshot_t indicator = {0};
    remote_controller_get_snapshot(&remote);
    runtime_animation_get_snapshot(&police);
    indicator_get_snapshot(&indicator);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "connected", remote.receiver.initialized);
    cJSON_AddBoolToObject(json, "mapping_complete", remote.receiver.mapping_complete);
    cJSON_AddBoolToObject(json, "discovery", remote.receiver.discovery_active);
    cJSON_AddBoolToObject(json, "vt", remote.receiver.vt_active);
    cJSON_AddNumberToObject(json, "event_drops", remote.event_drops);
    if (remote.has_last_button) {
        cJSON_AddNumberToObject(json, "last_button", (int)remote.last_button + 1);
        cJSON_AddStringToObject(json, "last_channel",
                                rf_physical_channel_name(remote.last_channel));
        cJSON_AddNumberToObject(json, "last_event_ms", (double)remote.last_event_ms);
        cJSON_AddBoolToObject(json, "last_action_accepted",
                              remote.last_action_accepted);
    } else {
        cJSON_AddNullToObject(json, "last_button");
        if (remote.receiver.has_last_channel) {
            cJSON_AddStringToObject(json, "last_channel",
                rf_physical_channel_name(remote.receiver.last_channel));
            cJSON_AddNumberToObject(json, "last_event_ms",
                                    (double)remote.receiver.last_event_ms);
        } else {
            cJSON_AddNullToObject(json, "last_channel");
            cJSON_AddNullToObject(json, "last_event_ms");
        }
        cJSON_AddNullToObject(json, "last_action_accepted");
    }
    cJSON_AddItemToObject(json, "button4", button4_json(&remote.config.button4));
    cJSON *mapping = cJSON_AddObjectToObject(json, "mapping");
    for (rf_physical_channel_t channel = RF_CHANNEL_D0;
         channel < RF_CHANNEL_COUNT; channel++) {
        const char *name = channel == RF_CHANNEL_D0 ? "d0" :
                           channel == RF_CHANNEL_D1 ? "d1" :
                           channel == RF_CHANNEL_D2 ? "d2" : "d3";
        remote_button_t button = remote.config.channel_map[channel];
        if (button >= REMOTE_BUTTON_1 && button < REMOTE_BUTTON_COUNT) {
            cJSON_AddNumberToObject(mapping, name, (int)button + 1);
        } else {
            cJSON_AddNullToObject(mapping, name);
        }
    }
    cJSON *police_json = cJSON_AddObjectToObject(json, "police");
    cJSON_AddStringToObject(police_json, "speed",
                            police_speed_name(remote.config.police_speed));
    cJSON_AddStringToObject(police_json, "state",
                            runtime_animation_state_name(police.state));
    cJSON_AddNumberToObject(police_json, "elapsed_ms", police.elapsed_ms);
    cJSON_AddBoolToObject(police_json, "timed_out", police.timed_out);
    cJSON *indicator_json = cJSON_AddObjectToObject(json, "indicator");
    cJSON_AddBoolToObject(indicator_json, "on", indicator.on);
    cJSON_AddStringToObject(indicator_json, "reason",
                            indicator_reason_name(indicator.reason));
    cJSON_AddNumberToObject(indicator_json, "gpio_level", indicator.gpio_level);
    cJSON_AddNumberToObject(indicator_json, "last_change_ms",
                            (double)indicator.last_change_ms);
    return json;
}

static cJSON *system_health_json(void)
{
    system_health_snapshot_t health = {0};
    system_health_get_snapshot(&health);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "healthy", health.healthy);
    cJSON_AddStringToObject(json, "reset_reason", health.reset_reason_name);
    cJSON_AddStringToObject(json, "previous_recovery_reason",
                            health.previous_recovery_reason);
    cJSON_AddStringToObject(json, "stale_component", health.stale_component);
    cJSON_AddNumberToObject(json, "supervisor_restarts",
                            health.supervisor_restart_count);
    cJSON_AddNumberToObject(json, "free_heap", health.free_heap);
    cJSON_AddNumberToObject(json, "minimum_free_heap", health.minimum_free_heap);
    cJSON *heartbeats = cJSON_AddObjectToObject(json, "heartbeats_ms");
    cJSON_AddNumberToObject(heartbeats, "connection_manager",
                            (double)health.connection_manager_heartbeat_ms);
    cJSON_AddNumberToObject(heartbeats, "group_runtime",
                            (double)health.group_runtime_heartbeat_ms);
    cJSON_AddNumberToObject(heartbeats, "rf_input", (double)health.rf_heartbeat_ms);
    cJSON_AddNumberToObject(heartbeats, "indicator",
                            (double)health.indicator_heartbeat_ms);
    cJSON_AddNumberToObject(heartbeats, "web_events", (double)health.web_heartbeat_ms);
    cJSON *counters = cJSON_AddObjectToObject(json, "counters");
    cJSON_AddNumberToObject(counters, "ble_forced_recoveries",
                            health.ble_forced_recoveries);
    cJSON_AddNumberToObject(counters, "ble_critical_event_replacements",
                            health.ble_critical_event_replacements);
    cJSON_AddNumberToObject(counters, "group_api_timeouts", health.group_api_timeouts);
    cJSON_AddNumberToObject(counters, "group_api_busy", health.group_api_busy);
    cJSON_AddNumberToObject(counters, "group_api_response_drops",
                            health.group_api_response_drops);
    cJSON_AddNumberToObject(counters, "rf_event_drops", health.rf_event_drops);
    cJSON_AddNumberToObject(counters, "websocket_event_drops",
                            health.websocket_event_drops);
    cJSON *indicator = cJSON_AddObjectToObject(json, "indicator");
    cJSON_AddNumberToObject(indicator, "gpio_level", health.indicator_gpio_level);
    cJSON_AddNumberToObject(indicator, "last_change_ms",
                            (double)health.indicator_last_change_ms);
    return json;
}

static cJSON *status_root(const sp624e_group_snapshot_t *snapshot)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "firmware", APP_FIRMWARE_VERSION);
    cJSON_AddNumberToObject(root, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(root, "wifi_clients", wifi_ap_client_count());
    cJSON *capabilities = cJSON_AddObjectToObject(root, "capabilities");
    cJSON_AddBoolToObject(capabilities, "rgb", true);
    cJSON_AddBoolToObject(capabilities, "white", snapshot->white_available);
    cJSON *group = cJSON_AddObjectToObject(root, "group");
    cJSON_AddStringToObject(group, "state", sp624e_group_state_name(snapshot->group_state));
    cJSON_AddNumberToObject(group, "generation", snapshot->desired.generation);
    cJSON_AddBoolToObject(group, "controller_started", snapshot->controller_started);
    cJSON_AddItemToObject(root, "left", side_status_json(&snapshot->sides[0]));
    cJSON_AddItemToObject(root, "right", side_status_json(&snapshot->sides[1]));
    cJSON *observed = cJSON_AddObjectToObject(root, "observed");
    cJSON_AddItemToObject(observed, "left", observed_json(&snapshot->sides[0].observed));
    cJSON_AddItemToObject(observed, "right", observed_json(&snapshot->sides[1].observed));
    cJSON_AddItemToObject(root, "remote", remote_json());
    cJSON_AddItemToObject(root, "system_health", system_health_json());
    return root;
}

static char *print_and_delete(cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}

char *web_json_status(void)
{
    sp624e_group_snapshot_t snapshot;
    sp624e_group_get_snapshot(&snapshot);
    return print_and_delete(status_root(&snapshot));
}

char *web_json_state(void)
{
    sp624e_group_snapshot_t snapshot;
    sp624e_group_get_snapshot(&snapshot);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "desired", desired_json(&snapshot.desired));
    cJSON *observed = cJSON_AddObjectToObject(root, "observed");
    cJSON_AddItemToObject(observed, "left", observed_json(&snapshot.sides[0].observed));
    cJSON_AddItemToObject(observed, "right", observed_json(&snapshot.sides[1].observed));
    cJSON *verified = cJSON_AddObjectToObject(root, "verified_generation");
    cJSON_AddNumberToObject(verified, "left", snapshot.sides[0].verified_generation);
    cJSON_AddNumberToObject(verified, "right", snapshot.sides[1].verified_generation);
    return print_and_delete(root);
}

char *web_json_presets(void)
{
    sp624e_group_snapshot_t snapshot;
    sp624e_group_get_snapshot(&snapshot);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "favorite", favorite_json());
    cJSON_AddBoolToObject(root, "white_available", snapshot.white_available);
    return print_and_delete(root);
}

char *web_json_snapshot_event(const char *type)
{
    sp624e_group_snapshot_t snapshot;
    sp624e_group_get_snapshot(&snapshot);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "firmware", APP_FIRMWARE_VERSION);
    cJSON_AddNumberToObject(root, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(root, "wifi_clients", wifi_ap_client_count());
    cJSON_AddStringToObject(root, "group_state", sp624e_group_state_name(snapshot.group_state));
    cJSON_AddNumberToObject(root, "generation", snapshot.desired.generation);
    cJSON *capabilities = cJSON_AddObjectToObject(root, "capabilities");
    cJSON_AddBoolToObject(capabilities, "rgb", true);
    cJSON_AddBoolToObject(capabilities, "white", snapshot.white_available);
    cJSON_AddItemToObject(root, "desired", desired_json(&snapshot.desired));
    cJSON_AddItemToObject(root, "left", side_status_json(&snapshot.sides[0]));
    cJSON_AddItemToObject(root, "right", side_status_json(&snapshot.sides[1]));
    cJSON *observed = cJSON_AddObjectToObject(root, "observed");
    cJSON_AddItemToObject(observed, "left", observed_json(&snapshot.sides[0].observed));
    cJSON_AddItemToObject(observed, "right", observed_json(&snapshot.sides[1].observed));
    cJSON_AddItemToObject(root, "favorite", favorite_json());
    cJSON_AddBoolToObject(root, "white_available", snapshot.white_available);
    cJSON_AddItemToObject(root, "remote", remote_json());
    cJSON_AddItemToObject(root, "system_health", system_health_json());
    return print_and_delete(root);
}

char *web_json_accepted(unsigned generation)
{
    sp624e_group_snapshot_t snapshot;
    sp624e_group_get_snapshot(&snapshot);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "accepted", true);
    cJSON_AddNumberToObject(root, "generation", generation);
    cJSON_AddStringToObject(root, "group_state", sp624e_group_state_name(snapshot.group_state));
    return print_and_delete(root);
}

char *web_json_error(const char *code, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "accepted", false);
    cJSON_AddStringToObject(root, "error", code);
    cJSON_AddStringToObject(root, "message", message);
    return print_and_delete(root);
}

char *web_json_remote(void)
{
    return print_and_delete(remote_json());
}

char *web_json_simple_accepted(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "accepted", true);
    return print_and_delete(root);
}
