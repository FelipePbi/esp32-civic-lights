#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
#else
typedef int esp_err_t;
#endif
#include "animation/police_animation.h"
#include "rf_input.h"

#define RF_REMOTE_CONFIG_VERSION 2u
#define RF_REMOTE_CONFIG_V1_ENCODED_SIZE 12u
#define RF_REMOTE_CONFIG_ENCODED_SIZE 13u

typedef enum {
    REMOTE_BUTTON_1 = 0,
    REMOTE_BUTTON_2,
    REMOTE_BUTTON_3,
    REMOTE_BUTTON_4,
    REMOTE_BUTTON_COUNT,
    REMOTE_BUTTON_INVALID = 0xff,
} remote_button_t;

typedef enum {
    REMOTE_ACTION_FAVORITE = 0,
    REMOTE_ACTION_RGB = 1,
    REMOTE_ACTION_WHITE = 2,
    REMOTE_ACTION_POLICE = 4,
    REMOTE_ACTION_COUNT = 5,
} remote_action_type_t;

typedef struct {
    remote_action_type_t type;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
} remote_button4_config_t;

typedef struct {
    uint32_t version;
    remote_button_t channel_map[RF_CHANNEL_COUNT];
    remote_button4_config_t button4;
    police_speed_t police_speed;
} rf_remote_config_t;

void rf_config_defaults(rf_remote_config_t *config);
bool rf_config_button4_valid(const remote_button4_config_t *config);
bool rf_config_mapping_valid(const remote_button_t mapping[RF_CHANNEL_COUNT],
                             bool require_complete);
bool rf_config_encode(const rf_remote_config_t *config,
                      uint8_t encoded[RF_REMOTE_CONFIG_ENCODED_SIZE]);
bool rf_config_decode(const uint8_t *encoded, size_t length,
                      rf_remote_config_t *config);
const char *remote_button_name(remote_button_t button);
const char *remote_action_type_name(remote_action_type_t type);
remote_button_t rf_config_resolve_button(const rf_remote_config_t *config,
                                         rf_physical_channel_t channel,
                                         bool discovery_active);

esp_err_t rf_config_init(void);
void rf_config_get(rf_remote_config_t *config);
esp_err_t rf_config_save_button4(const remote_button4_config_t *button4);
esp_err_t rf_config_save_police_speed(police_speed_t speed);
esp_err_t rf_config_save_mapping(const remote_button_t mapping[RF_CHANNEL_COUNT]);
