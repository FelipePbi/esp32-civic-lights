#include <inttypes.h>

#include "app_config.h"
#include "ble/ble_connection_manager.h"
#include "ble/ble_scanner.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "console/runtime_console.h"
#include "diagnostics/ble_diagnostics.h"
#include "presets/preset_manager.h"
#include "sdkconfig.h"
#include "wifi/wifi_ap.h"
#include "web/web_server.h"
#include "web/websocket.h"
#include "animation/animation_manager.h"
#include "indicator/indicator.h"
#include "interior/interior_light.h"
#include "remote/remote_controller.h"
#include "diagnostics/system_health.h"

static const char *TAG = "SYSTEM";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    esp_chip_info_t chip;
    uint32_t flash_size = 0;
    esp_chip_info(&chip);
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));

    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, " SP624E Controller");
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Firmware: %s", APP_FIRMWARE_VERSION);
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip target: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "Revision: %u", chip.revision);
    ESP_LOGI(TAG, "CPU cores: %u", chip.cores);
    ESP_LOGI(TAG, "Flash: %" PRIu32 " MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "Heap before Wi-Fi: free=%" PRIu32 " minimum=%" PRIu32 " bytes",
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "SP624E application writes: CONTROLLED (snapshot required)");

    init_nvs();
    ESP_ERROR_CHECK(runtime_console_start());
    ESP_ERROR_CHECK(ble_diagnostics_init());
    esp_err_t preset_err = preset_manager_init();
    if (preset_err != ESP_OK) {
        ESP_LOGW(TAG, "Favorite preset load ignored: %s", esp_err_to_name(preset_err));
    }
    animation_manager_init(websocket_publish);
    ESP_ERROR_CHECK(indicator_init());
    ESP_ERROR_CHECK(interior_light_init());
    ESP_ERROR_CHECK(ble_connection_manager_add_master_guard(interior_light_release_master));
    ESP_ERROR_CHECK(wifi_ap_start());
    ESP_LOGI(TAG, "Heap after Wi-Fi: free=%" PRIu32 " minimum=%" PRIu32 " bytes",
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI(TAG, "Heap with HTTP/WebSocket: free=%" PRIu32 " minimum=%" PRIu32 " bytes",
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    ESP_ERROR_CHECK(remote_controller_init(websocket_publish));
    ESP_ERROR_CHECK(ble_scanner_start());
    ESP_ERROR_CHECK(system_health_init());
    ESP_LOGI(TAG, "System initialized successfully.");
}
