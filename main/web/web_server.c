#include "web_server.h"

#include <stdio.h>
#include <string.h>

#include "api_handlers.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "websocket.h"

#define WEB_BASE_PATH "/web"
#define WEB_PATH_MAX 192

static const char *TAG = "HTTP";
static httpd_handle_t s_server;

static const char *content_type(const char *path)
{
    const char *extension = strrchr(path, '.');
    if (extension == NULL) return "application/octet-stream";
    if (strcmp(extension, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(extension, ".js") == 0) return "text/javascript; charset=utf-8";
    if (strcmp(extension, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(extension, ".json") == 0 || strcmp(extension, ".webmanifest") == 0) {
        return "application/manifest+json; charset=utf-8";
    }
    if (strcmp(extension, ".svg") == 0) return "image/svg+xml";
    if (strcmp(extension, ".png") == 0) return "image/png";
    return "application/octet-stream";
}

static esp_err_t static_get(httpd_req_t *req)
{
    if (strstr(req->uri, "..") != NULL) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                                                    "Invalid path");
    char path[WEB_PATH_MAX];
    const char *uri = strcmp(req->uri, "/") == 0 ? "/index.html" : req->uri;
    if (snprintf(path, sizeof(path), WEB_BASE_PATH "%s", uri) >= sizeof(path)) {
        return httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Path too long");
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL && strncmp(uri, "/api/", 5) == 0) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "API route not found");
    }
    if (file == NULL && strchr(uri, '.') == NULL) {
        strlcpy(path, WEB_BASE_PATH "/index.html", sizeof(path));
        file = fopen(path, "rb");
    }
    if (file == NULL) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    httpd_resp_set_type(req, content_type(path));
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Cache-Control",
                       strstr(path, "/assets/") != NULL ?
                       "public, max-age=31536000, immutable" : "no-cache");
    char chunk[1024];
    size_t count;
    esp_err_t result = ESP_OK;
    while ((count = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        result = httpd_resp_send_chunk(req, chunk, count);
        if (result != ESP_OK) break;
    }
    fclose(file);
    if (result == ESP_OK) result = httpd_resp_send_chunk(req, NULL, 0);
    return result;
}

esp_err_t web_server_start(void)
{
    esp_vfs_spiffs_conf_t spiffs = {
        .base_path = WEB_BASE_PATH,
        .partition_label = "web",
        .max_files = 8,
        .format_if_mount_failed = false,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&spiffs), TAG, "mount web SPIFFS");
    size_t total = 0, used = 0;
    ESP_RETURN_ON_ERROR(esp_spiffs_info("web", &total, &used), TAG, "SPIFFS info");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 24;
    config.max_open_sockets = 10;
    config.lru_purge_enable = true;
    config.stack_size = 6144;
    config.uri_match_fn = httpd_uri_match_wildcard;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "start HTTP server");
    ESP_RETURN_ON_ERROR(api_handlers_register(s_server), TAG, "register API");
    ESP_RETURN_ON_ERROR(websocket_register(s_server), TAG, "register WebSocket");
    const httpd_uri_t files = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_get,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &files), TAG,
                        "register static files");
    websocket_start_events(s_server);
    ESP_LOGI(TAG, "ready http://192.168.4.1 SPIFFS=%u/%u bytes",
             (unsigned)used, (unsigned)total);
    return ESP_OK;
}
