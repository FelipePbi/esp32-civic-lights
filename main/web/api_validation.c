#include "api_validation.h"

#include <stdbool.h>
#include <string.h>

#include "cJSON.h"

static bool byte_value(const cJSON *root, const char *name, uint8_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 255 ||
        item->valuedouble != (double)item->valueint) return false;
    *value = (uint8_t)item->valueint;
    return true;
}

bool web_api_body_length_valid(size_t length)
{
    return length > 0 && length <= WEB_API_BODY_MAX;
}

bool web_api_parse_state(const char *json, size_t length, web_state_request_t *request)
{
    if (json == NULL || length == 0 || request == NULL) return false;
    memset(request, 0, sizeof(*request));
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(root, "mode");
    bool valid = false;
    if (cJSON_IsString(mode) && strcmp(mode->valuestring, "rgb") == 0) {
        request->mode = WEB_STATE_RGB;
        valid = byte_value(root, "r", &request->red) &&
                byte_value(root, "g", &request->green) &&
                byte_value(root, "b", &request->blue) &&
                byte_value(root, "brightness", &request->brightness);
    } else if (cJSON_IsString(mode) && strcmp(mode->valuestring, "white") == 0) {
        request->mode = WEB_STATE_WHITE;
        valid = byte_value(root, "brightness", &request->brightness);
    }
    cJSON_Delete(root);
    return valid;
}

bool web_api_parse_favorite(const char *json, size_t length, web_state_request_t *request)
{
    return web_api_parse_state(json, length, request) && request->mode == WEB_STATE_RGB;
}

bool web_api_parse_remote_button4(const char *json, size_t length,
                                  remote_button4_config_t *config)
{
    if (json == NULL || config == NULL || length == 0) return false;
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    bool valid = cJSON_IsObject(root) && cJSON_IsString(type);
    remote_button4_config_t parsed = {
        .type = REMOTE_ACTION_FAVORITE,
        .red = 255,
        .brightness = 64,
    };
    if (valid && strcmp(type->valuestring, "favorite") == 0) {
        parsed.type = REMOTE_ACTION_FAVORITE;
    } else if (valid && strcmp(type->valuestring, "rgb") == 0) {
        parsed.type = REMOTE_ACTION_RGB;
        valid = byte_value(root, "r", &parsed.red) &&
                byte_value(root, "g", &parsed.green) &&
                byte_value(root, "b", &parsed.blue) &&
                byte_value(root, "brightness", &parsed.brightness);
    } else if (valid && strcmp(type->valuestring, "white") == 0) {
        parsed.type = REMOTE_ACTION_WHITE;
        valid = byte_value(root, "brightness", &parsed.brightness);
    } else if (valid && strcmp(type->valuestring, "police") == 0) {
        parsed.type = REMOTE_ACTION_POLICE;
    } else {
        valid = false;
    }
    valid = valid && rf_config_button4_valid(&parsed);
    if (valid) *config = parsed;
    cJSON_Delete(root);
    return valid;
}

bool web_api_parse_police_speed(const char *json, size_t length,
                                police_speed_t *speed)
{
    if (json == NULL || speed == NULL || length == 0) return false;
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "speed");
    police_speed_t parsed = cJSON_IsString(value) ?
        police_speed_from_name(value->valuestring) : POLICE_SPEED_COUNT;
    bool valid = cJSON_IsObject(root) && police_speed_valid(parsed);
    if (valid) *speed = parsed;
    cJSON_Delete(root);
    return valid;
}

static rf_physical_channel_t parse_channel(const cJSON *root, const char *name)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(value)) return RF_CHANNEL_INVALID;
    if (strcmp(value->valuestring, "d0") == 0) return RF_CHANNEL_D0;
    if (strcmp(value->valuestring, "d1") == 0) return RF_CHANNEL_D1;
    if (strcmp(value->valuestring, "d2") == 0) return RF_CHANNEL_D2;
    if (strcmp(value->valuestring, "d3") == 0) return RF_CHANNEL_D3;
    return RF_CHANNEL_INVALID;
}

bool web_api_parse_remote_mapping(const char *json, size_t length,
                                  remote_button_t mapping[RF_CHANNEL_COUNT])
{
    if (json == NULL || mapping == NULL || length == 0) return false;
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    for (size_t i = 0; i < RF_CHANNEL_COUNT; ++i) mapping[i] = REMOTE_BUTTON_INVALID;
    const char *names[REMOTE_BUTTON_COUNT] = {"button1", "button2", "button3", "button4"};
    bool valid = true;
    for (size_t button = 0; button < REMOTE_BUTTON_COUNT; ++button) {
        rf_physical_channel_t channel = parse_channel(root, names[button]);
        if (channel == RF_CHANNEL_INVALID || mapping[channel] != REMOTE_BUTTON_INVALID) {
            valid = false;
            break;
        }
        mapping[channel] = (remote_button_t)button;
    }
    valid = valid && rf_config_mapping_valid(mapping, true);
    cJSON_Delete(root);
    return valid;
}
