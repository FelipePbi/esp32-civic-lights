#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEB_API_BODY_MAX 384u
#include "remote/rf_config.h"

typedef enum {
    WEB_STATE_RGB = 0,
    WEB_STATE_WHITE,
} web_state_mode_t;

typedef struct {
    web_state_mode_t mode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
} web_state_request_t;

bool web_api_parse_state(const char *json, size_t length, web_state_request_t *request);
bool web_api_parse_favorite(const char *json, size_t length, web_state_request_t *request);
bool web_api_body_length_valid(size_t length);
bool web_api_parse_remote_button4(const char *json, size_t length,
                                  remote_button4_config_t *config);
bool web_api_parse_police_speed(const char *json, size_t length,
                                police_speed_t *speed);
bool web_api_parse_remote_mapping(const char *json, size_t length,
                                  remote_button_t mapping[RF_CHANNEL_COUNT]);
