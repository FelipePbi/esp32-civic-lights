#include "api_handlers.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "api_validation.h"
#include "json_codec.h"
#include "presets/preset_manager.h"
#include "sp624e/sp624e_controller.h"
#include "websocket.h"
#include "remote/rf_config.h"
#include "remote/rf_remote.h"
#include "indicator/indicator.h"

static const char *TAG = "WEB_API";

static esp_err_t send_json(httpd_req_t *req, const char *status, char *json)
{
    if (json == NULL) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (status != NULL) httpd_resp_set_status(req, status);
    esp_err_t result = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return result;
}

static esp_err_t send_error(httpd_req_t *req, const char *status,
                            const char *code, const char *message)
{
    return send_json(req, status, web_json_error(code, message));
}

static bool receive_body(httpd_req_t *req, char body[WEB_API_BODY_MAX + 1], size_t *length)
{
    if (!web_api_body_length_valid((size_t)req->content_len)) return false;
    size_t received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return false;
        received += (size_t)count;
    }
    body[received] = '\0';
    *length = received;
    return true;
}

static esp_err_t status_get(httpd_req_t *req)
{
    return send_json(req, NULL, web_json_status());
}

static esp_err_t state_get(httpd_req_t *req)
{
    return send_json(req, NULL, web_json_state());
}

static esp_err_t state_put(httpd_req_t *req)
{
    char body[WEB_API_BODY_MAX + 1];
    size_t body_length = 0;
    web_state_request_t state;
    if (!receive_body(req, body, &body_length) ||
        !web_api_parse_state(body, body_length, &state)) {
        ESP_LOGW(TAG, "Rejected invalid state payload length=%d", req->content_len);
        return send_error(req, "400 Bad Request", "invalid_json",
                                        "Expected a small JSON object");
    }
    sp624e_group_api_result_t api_result = SP624E_GROUP_API_UNSUPPORTED;
    uint32_t generation = 0;
    if (state.mode == WEB_STATE_RGB) {
        api_result = sp624e_group_set_rgb(state.red, state.green, state.blue,
                                          state.brightness, &generation);
    } else {
        api_result = sp624e_group_set_white(state.brightness, &generation);
    }
    if (api_result == SP624E_GROUP_API_UNSUPPORTED) {
        return send_error(req, "409 Conflict", "white_unavailable",
                          "Real white is disabled until physical validation passes");
    }
    if (api_result == SP624E_GROUP_API_NOT_READY) {
        return send_error(req, "503 Service Unavailable", "controller_not_ready",
                          "BLE group controller has not started");
    }
    if (api_result == SP624E_GROUP_API_TIMEOUT) {
        return send_error(req, "504 Gateway Timeout", "controller_timeout",
                          "Group controller did not respond in time");
    }
    if (api_result != SP624E_GROUP_API_OK) {
        return send_error(req, "503 Service Unavailable", "controller_busy",
                          "Command queue did not accept the request");
    }
    return send_json(req, "202 Accepted", web_json_accepted(generation));
}

static esp_err_t presets_get(httpd_req_t *req)
{
    return send_json(req, NULL, web_json_presets());
}

static esp_err_t favorite_put(httpd_req_t *req)
{
    char body[WEB_API_BODY_MAX + 1];
    size_t body_length = 0;
    web_state_request_t state;
    if (!receive_body(req, body, &body_length) ||
        !web_api_parse_favorite(body, body_length, &state)) {
        ESP_LOGW(TAG, "Rejected invalid favorite payload length=%d", req->content_len);
        return send_error(req, "400 Bad Request", "invalid_json",
                                        "Expected a small JSON object");
    }
    sp624e_favorite_preset_t preset = {
        .version = SP624E_FAVORITE_VERSION, .red = state.red, .green = state.green,
        .blue = state.blue, .brightness = state.brightness,
    };
    esp_err_t result = preset_manager_save_favorite(&preset);
    if (result != ESP_OK) return send_error(req, "500 Internal Server Error", "nvs_error",
                                            "Favorite could not be persisted");
    websocket_publish("favorite_updated");
    return send_json(req, NULL, web_json_presets());
}

static esp_err_t resync_post(httpd_req_t *req)
{
    uint32_t generation = 0;
    sp624e_group_api_result_t result = sp624e_group_force_resync(&generation);
    if (result == SP624E_GROUP_API_NOT_READY) {
        return send_error(req, "503 Service Unavailable", "controller_not_ready",
                          "BLE group controller has not started");
    }
    if (result == SP624E_GROUP_API_TIMEOUT) {
        return send_error(req, "504 Gateway Timeout", "controller_timeout",
                          "Group controller did not respond in time");
    }
    if (result != SP624E_GROUP_API_OK) {
        return send_error(req, "503 Service Unavailable", "controller_busy",
                          "Resync request was not accepted");
    }
    return send_json(req, "202 Accepted", web_json_accepted(generation));
}

static esp_err_t remote_get(httpd_req_t *req)
{
    return send_json(req, NULL, web_json_remote());
}

static esp_err_t remote_button4_put(httpd_req_t *req)
{
    char body[WEB_API_BODY_MAX + 1];
    size_t length = 0;
    remote_button4_config_t config;
    if (!receive_body(req, body, &length) ||
        !web_api_parse_remote_button4(body, length, &config)) {
        return send_error(req, "400 Bad Request", "invalid_remote_config",
                          "Expected favorite, rgb, white, or police");
    }
    esp_err_t result = rf_config_save_button4(&config);
    if (result != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "nvs_error",
                          "Button 4 config could not be persisted");
    }
    ESP_LOGI(TAG, "Button 4 persisted action=%s", remote_action_type_name(config.type));
    websocket_publish("remote_config_updated");
    return send_json(req, NULL, web_json_remote());
}

static esp_err_t remote_police_put(httpd_req_t *req)
{
    char body[WEB_API_BODY_MAX + 1];
    size_t length = 0;
    police_speed_t speed;
    if (!receive_body(req, body, &length) ||
        !web_api_parse_police_speed(body, length, &speed)) {
        return send_error(req, "400 Bad Request", "invalid_police_speed",
                          "Expected slow, normal, fast, or very_fast");
    }
    esp_err_t result = rf_config_save_police_speed(speed);
    if (result != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "nvs_error",
                          "Police speed could not be persisted");
    }
    ESP_LOGI(TAG, "Police speed persisted speed=%s", police_speed_name(speed));
    websocket_publish("remote_config_updated");
    return send_json(req, NULL, web_json_remote());
}

static esp_err_t remote_mapping_put(httpd_req_t *req)
{
    char body[WEB_API_BODY_MAX + 1];
    size_t length = 0;
    remote_button_t mapping[RF_CHANNEL_COUNT];
    if (!receive_body(req, body, &length) ||
        !web_api_parse_remote_mapping(body, length, mapping)) {
        return send_error(req, "400 Bad Request", "invalid_remote_mapping",
                          "Expected unique d0-d3 mapping for buttons 1-4");
    }
    esp_err_t result = rf_config_save_mapping(mapping);
    if (result != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "nvs_error",
                          "RF mapping could not be persisted");
    }
    ESP_LOGI(TAG, "RF physical mapping persisted");
    websocket_publish("remote_config_updated");
    return send_json(req, NULL, web_json_remote());
}

static esp_err_t indicator_test_post(httpd_req_t *req)
{
    if (!indicator_run_self_test()) {
        return send_error(req, "409 Conflict", "indicator_test_busy",
                          "Indicator self-test is already running");
    }
    websocket_publish("indicator_test_started");
    return send_json(req, "202 Accepted", web_json_simple_accepted());
}

static esp_err_t remote_discovery_start_post(httpd_req_t *req)
{
    if (!rf_remote_set_discovery(true)) {
        return send_error(req, "503 Service Unavailable", "receiver_not_ready",
                          "RF receiver is not initialized");
    }
    websocket_publish("remote_discovery_started");
    return send_json(req, "202 Accepted", web_json_remote());
}

static esp_err_t remote_discovery_stop_post(httpd_req_t *req)
{
    if (!rf_remote_set_discovery(false)) {
        return send_error(req, "503 Service Unavailable", "receiver_not_ready",
                          "RF receiver is not initialized");
    }
    websocket_publish("remote_discovery_stopped");
    return send_json(req, "202 Accepted", web_json_remote());
}

esp_err_t api_handlers_register(httpd_handle_t server)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/v1/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/v1/state", .method = HTTP_GET, .handler = state_get},
        {.uri = "/api/v1/state", .method = HTTP_PUT, .handler = state_put},
        {.uri = "/api/v1/presets", .method = HTTP_GET, .handler = presets_get},
        {.uri = "/api/v1/presets/favorite", .method = HTTP_PUT, .handler = favorite_put},
        {.uri = "/api/v1/resync", .method = HTTP_POST, .handler = resync_post},
        {.uri = "/api/v1/remote", .method = HTTP_GET, .handler = remote_get},
        {.uri = "/api/v1/remote/button4", .method = HTTP_PUT,
         .handler = remote_button4_put},
        {.uri = "/api/v1/remote/police", .method = HTTP_PUT,
         .handler = remote_police_put},
        {.uri = "/api/v1/remote/mapping", .method = HTTP_PUT,
         .handler = remote_mapping_put},
        {.uri = "/api/v1/indicator/test", .method = HTTP_POST,
         .handler = indicator_test_post},
        {.uri = "/api/v1/remote/discovery/start", .method = HTTP_POST,
         .handler = remote_discovery_start_post},
        {.uri = "/api/v1/remote/discovery/stop", .method = HTTP_POST,
         .handler = remote_discovery_stop_post},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        esp_err_t result = httpd_register_uri_handler(server, &handlers[i]);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}
