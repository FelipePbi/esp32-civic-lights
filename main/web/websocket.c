#include "websocket.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "json_codec.h"
#include "sp624e/sp624e_controller.h"
#include "wifi/wifi_ap.h"
#include "animation/runtime_animation.h"
#include "indicator/indicator.h"

#define WS_MAX_CLIENTS 8
#define WS_EVENT_QUEUE_DEPTH 16
#define WS_EVENT_TYPE_MAX 40

static const char *TAG = "WS";
static httpd_handle_t s_server;

typedef struct {
    httpd_handle_t server;
    char type[WS_EVENT_TYPE_MAX];
} broadcast_work_t;

typedef struct {
    char type[WS_EVENT_TYPE_MAX];
} websocket_event_t;

static StaticQueue_t s_event_queue_storage;
static uint8_t s_event_queue_buffer[WS_EVENT_QUEUE_DEPTH * sizeof(websocket_event_t)];
static QueueHandle_t s_event_queue;
static int64_t s_event_heartbeat_ms;
static uint32_t s_event_drops;
static portMUX_TYPE s_health_mux = portMUX_INITIALIZER_UNLOCKED;

static void broadcast_in_http_task(void *arg)
{
    broadcast_work_t *work = arg;
    size_t count = WS_MAX_CLIENTS;
    int fds[WS_MAX_CLIENTS];
    if (httpd_get_client_list(work->server, &count, fds) == ESP_OK) {
        char *text = NULL;
        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
        };
        for (size_t i = 0; i < count; ++i) {
            if (httpd_ws_get_fd_info(work->server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                if (text == NULL) {
                    text = web_json_snapshot_event(work->type);
                    if (text == NULL) break;
                    frame.payload = (uint8_t *)text;
                    frame.len = strlen(text);
                }
                if (httpd_ws_send_frame_async(work->server, fds[i], &frame) != ESP_OK) {
                    ESP_LOGW(TAG, "send failed fd=%d", fds[i]);
                }
            }
        }
        free(text);
    }
    free(work);
}

static void queue_broadcast_work(const char *type)
{
    if (s_server == NULL || type == NULL) return;
    broadcast_work_t *work = calloc(1, sizeof(*work));
    if (work == NULL) return;
    work->server = s_server;
    snprintf(work->type, sizeof(work->type), "%s", type);
    if (httpd_queue_work(s_server, broadcast_in_http_task, work) != ESP_OK) {
        free(work);
    }
}

void websocket_publish(const char *type)
{
    if (s_event_queue == NULL || type == NULL) return;
    websocket_event_t event = {0};
    snprintf(event.type, sizeof(event.type), "%s", type);
    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_health_mux);
        s_event_drops++;
        portEXIT_CRITICAL(&s_health_mux);
    }
}

static esp_err_t websocket_handler(httpd_req_t *req)
{
    httpd_ws_frame_t frame = {0};
    esp_err_t result = httpd_ws_recv_frame(req, &frame, 0);
    if (result != ESP_OK) return result;
    if (frame.len > 512) return ESP_ERR_INVALID_SIZE;
    if (frame.len > 0) {
        frame.payload = malloc(frame.len + 1);
        if (frame.payload == NULL) return ESP_ERR_NO_MEM;
        result = httpd_ws_recv_frame(req, &frame, frame.len);
        free(frame.payload);
    }
    return result;
}

static esp_err_t websocket_connected(httpd_req_t *req)
{
    char *snapshot = web_json_snapshot_event("snapshot");
    if (snapshot == NULL) return ESP_ERR_NO_MEM;
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)snapshot,
        .len = strlen(snapshot),
    };
    esp_err_t result = httpd_ws_send_frame(req, &frame);
    free(snapshot);
    ESP_LOGI(TAG, "client connected fd=%d", httpd_req_to_sockfd(req));
    return result;
}

static bool connection_changed(const sp624e_side_snapshot_t *a,
                               const sp624e_side_snapshot_t *b)
{
    return a->connection.state != b->connection.state ||
           a->connection.connected != b->connection.connected ||
           a->connection.rssi != b->connection.rssi ||
           a->verified_generation != b->verified_generation;
}

static void event_task(void *arg)
{
    (void)arg;
    bool watched = esp_task_wdt_add(NULL) == ESP_OK;
    sp624e_group_snapshot_t previous = {0};
    runtime_animation_snapshot_t previous_runtime = {0};
    indicator_snapshot_t previous_indicator = {0};
    uint8_t previous_wifi = 0;
    bool have_previous = false;
    while (true) {
        websocket_event_t event;
        if (xQueueReceive(s_event_queue, &event, pdMS_TO_TICKS(250)) == pdTRUE) {
            queue_broadcast_work(event.type);
            while (xQueueReceive(s_event_queue, &event, 0) == pdTRUE) {
                queue_broadcast_work(event.type);
            }
        }
        sp624e_group_snapshot_t current;
        runtime_animation_snapshot_t runtime = {0};
        indicator_snapshot_t indicator = {0};
        sp624e_group_get_snapshot(&current);
        runtime_animation_get_snapshot(&runtime);
        indicator_get_snapshot(&indicator);
        uint8_t wifi = wifi_ap_client_count();
        if (have_previous) {
            if (wifi != previous_wifi) {
                websocket_publish(wifi > previous_wifi ? "wifi_client_connected" :
                                                         "wifi_client_disconnected");
            }
            if (current.group_state != previous.group_state) {
                websocket_publish("group_status");
                if (current.group_state == SP624E_GROUP_SYNCED) {
                    websocket_publish("sync_complete");
                } else if (current.group_state == SP624E_GROUP_ERROR) {
                    websocket_publish("sync_failed");
                }
            }
            if (memcmp(&current.desired, &previous.desired, sizeof(current.desired)) != 0) {
                websocket_publish("desired_state");
            }
            if (current.white_available != previous.white_available) {
                websocket_publish("favorite_updated");
            }
            if (connection_changed(&current.sides[0], &previous.sides[0]) ||
                connection_changed(&current.sides[1], &previous.sides[1])) {
                websocket_publish("controller_status");
            }
            if (!sp624e_state_equal(&current.sides[0].observed,
                                    &previous.sides[0].observed) ||
                !sp624e_state_equal(&current.sides[1].observed,
                                    &previous.sides[1].observed)) {
                websocket_publish("observed_state");
            }
            if (runtime.state != previous_runtime.state ||
                runtime.timed_out != previous_runtime.timed_out) {
                websocket_publish("runtime_animation");
            }
            if (indicator.on != previous_indicator.on ||
                indicator.reason != previous_indicator.reason) {
                websocket_publish("indicator_status");
            }
        }
        previous = current;
        previous_runtime = runtime;
        previous_indicator = indicator;
        previous_wifi = wifi;
        have_previous = true;
        portENTER_CRITICAL(&s_health_mux);
        s_event_heartbeat_ms = esp_timer_get_time() / 1000;
        portEXIT_CRITICAL(&s_health_mux);
        if (watched) esp_task_wdt_reset();
    }
}

esp_err_t websocket_register(httpd_handle_t server)
{
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .ws_post_handshake_cb = websocket_connected,
    };
    return httpd_register_uri_handler(server, &ws);
}

void websocket_start_events(httpd_handle_t server)
{
    s_server = server;
    s_event_queue = xQueueCreateStatic(WS_EVENT_QUEUE_DEPTH, sizeof(websocket_event_t),
                                       s_event_queue_buffer, &s_event_queue_storage);
    xTaskCreate(event_task, "web_events", 4096, NULL, 3, NULL);
}

int64_t websocket_heartbeat_ms(void)
{
    portENTER_CRITICAL(&s_health_mux);
    int64_t heartbeat = s_event_heartbeat_ms;
    portEXIT_CRITICAL(&s_health_mux);
    return heartbeat;
}

uint32_t websocket_event_drop_count(void)
{
    portENTER_CRITICAL(&s_health_mux);
    uint32_t drops = s_event_drops;
    portEXIT_CRITICAL(&s_health_mux);
    return drops;
}
