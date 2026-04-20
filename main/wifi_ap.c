#include "wifi_ap.h"
#include "config.h"
#include "config_mgr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"

#include <string.h>
#define AP_STARTED_BIT  BIT0

static EventGroupHandle_t s_wifi_event_group;
static bool               s_initialized = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(LOG_TAG_WIFI, "Soft AP started (SSID: %s)", config_get_ssid());
                xEventGroupSetBits(s_wifi_event_group, AP_STARTED_BIT);
                break;

            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(LOG_TAG_WIFI, "Soft AP stopped");
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *ev = event_data;
                ESP_LOGI(LOG_TAG_WIFI, "Station connected — MAC: %02X:%02X:%02X:%02X:%02X:%02X AID:%d",
                     ev->mac[0], ev->mac[1], ev->mac[2],
                     ev->mac[3], ev->mac[4], ev->mac[5],
                     ev->aid);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *ev = event_data;
                ESP_LOGI(LOG_TAG_WIFI, "Station disconnected — MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     ev->mac[0], ev->mac[1], ev->mac[2],
                     ev->mac[3], ev->mac[4], ev->mac[5]);
                break;
            }
            default:
                break;
        }
    }
}

static void apply_ap_config(void)
{
    wifi_config_t wifi_cfg = { 0 };

    strncpy((char *)wifi_cfg.ap.ssid, config_get_ssid(), sizeof(wifi_cfg.ap.ssid) - 1);
    strncpy((char *)wifi_cfg.ap.password, config_get_pass(), sizeof(wifi_cfg.ap.password) - 1);

    wifi_cfg.ap.ssid_len       = (uint8_t)strlen(config_get_ssid());
    wifi_cfg.ap.channel        = AP_CHANNEL;
    wifi_cfg.ap.max_connection = AP_MAX_CONNECTIONS;
    wifi_cfg.ap.beacon_interval= AP_BEACON_INTERVAL_MS;

    // Nếu password rỗng hoặc quá ngắn → Open AP
    if (strlen(config_get_pass()) < 8) {
        wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;
        ESP_LOGW(LOG_TAG_WIFI, "Password too short — AP will be OPEN (no auth)");
    } else {
        wifi_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
}

void wifi_ap_init(void)
{
    if (s_initialized) return;

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    esp_err_t err = esp_netif_dhcps_stop(ap_netif);
    if (err == ESP_OK) {
        ESP_LOGI(LOG_TAG_WIFI, "DHCP Server has been DISABLED.");
    } else {
        ESP_LOGE(LOG_TAG_WIFI, "Failed to stop DHCP Server: %s", esp_err_to_name(err));
    }

    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
    ip_info.ip.addr = ipaddr_addr(config_get_ip());
    ip_info.gw.addr = ipaddr_addr(config_get_ip());
    ip_info.netmask.addr = ipaddr_addr("255.255.255.0");

    esp_netif_set_ip_info(ap_netif, &ip_info);

    if (config_get_dhcp_enable()) {
        esp_netif_dhcps_start(ap_netif);
        ESP_LOGI(LOG_TAG_WIFI, "DHCP Server: ENABLED");
    } else {
        ESP_LOGI(LOG_TAG_WIFI, "DHCP Server: DISABLED (Clients need static IP)");
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, NULL, NULL);

    esp_wifi_set_mode(WIFI_MODE_AP);

    apply_ap_config();

    esp_wifi_start();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           AP_STARTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    if (!(bits & AP_STARTED_BIT)) {
        ESP_LOGE(LOG_TAG_WIFI, "WiFi AP start timeout!");
    }

    s_initialized = true;
    ESP_LOGI(LOG_TAG_WIFI, "WiFi AP ready — SSID: %s  IP: %s  DHCP: %s", 
                config_get_ssid(), 
                config_get_ip(), 
                config_get_dhcp_enable() ? "ON" : "OFF");
}

void wifi_ap_stop(void)
{
    esp_wifi_stop();
    s_initialized = false;
    xEventGroupClearBits(s_wifi_event_group, AP_STARTED_BIT);
}

void wifi_ap_restart(void)
{
    ESP_LOGI(LOG_TAG_WIFI, "Restarting WiFi AP with new config...");
    wifi_ap_stop();

    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_dhcps_stop(ap_netif);

        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
        ip_info.ip.addr = ipaddr_addr(config_get_ip());
        ip_info.gw.addr = ipaddr_addr(config_get_ip());
        ip_info.netmask.addr = ipaddr_addr("255.255.255.0");
        
        esp_netif_set_ip_info(ap_netif, &ip_info);

        if (config_get_dhcp_enable()) {
            esp_netif_dhcps_start(ap_netif);
            ESP_LOGI(LOG_TAG_WIFI, "DHCP Server: ON");
        } else {
            ESP_LOGI(LOG_TAG_WIFI, "DHCP Server: OFF");
        }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    apply_ap_config();
    esp_wifi_start();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           AP_STARTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    if (!(bits & AP_STARTED_BIT)) {
        ESP_LOGE(LOG_TAG_WIFI, "WiFi AP restart timeout!");
    } else {
        s_initialized = true;
    ESP_LOGI(LOG_TAG_WIFI, "WiFi AP restarted OK — SSID: %s, IP: %s", 
                    config_get_ssid(), config_get_ip());
    }
}
