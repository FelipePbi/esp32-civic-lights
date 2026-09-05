#include "wifi_ap.h"

#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "WIFI";
static volatile uint8_t s_clients;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        if (s_clients < UINT8_MAX) s_clients++;
        const wifi_event_ap_staconnected_t *event = data;
        ESP_LOGI(TAG, "client connected aid=%u total=%u", event->aid, s_clients);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_clients > 0) s_clients--;
        const wifi_event_ap_stadisconnected_t *event = data;
        ESP_LOGI(TAG, "client disconnected aid=%u total=%u", event->aid, s_clients);
    }
}

esp_err_t wifi_ap_start(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    esp_err_t event_result = esp_event_loop_create_default();
    if (event_result != ESP_OK && event_result != ESP_ERR_INVALID_STATE) {
        return event_result;
    }
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    if (netif == NULL) return ESP_FAIL;

    ESP_RETURN_ON_ERROR(esp_netif_dhcps_stop(netif), TAG, "stop DHCP server");
    esp_netif_ip_info_t ip = {0};
    if (!ip4addr_aton(APP_WIFI_AP_IP, (ip4_addr_t *)&ip.ip) ||
        !ip4addr_aton("255.255.255.0", (ip4_addr_t *)&ip.netmask) ||
        !ip4addr_aton(APP_WIFI_AP_IP, (ip4_addr_t *)&ip.gw)) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(netif, &ip), TAG, "set AP address");
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_start(netif), TAG, "start DHCP server");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_event, NULL),
                        TAG, "event handler");
    wifi_config_t config = {0};
    strlcpy((char *)config.ap.ssid, APP_WIFI_AP_SSID, sizeof(config.ap.ssid));
    strlcpy((char *)config.ap.password, APP_WIFI_AP_PASSWORD,
            sizeof(config.ap.password));
    config.ap.ssid_len = strlen(APP_WIFI_AP_SSID);
    config.ap.channel = APP_WIFI_AP_CHANNEL;
    config.ap.max_connection = APP_WIFI_AP_MAX_CLIENTS;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "set AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start AP");
    ESP_LOGI(TAG, "ready SSID=%s IP=%s channel=%d WPA2 clients=%d",
             APP_WIFI_AP_SSID, APP_WIFI_AP_IP, APP_WIFI_AP_CHANNEL,
             APP_WIFI_AP_MAX_CLIENTS);
    return ESP_OK;
}

uint8_t wifi_ap_client_count(void)
{
    return s_clients;
}
