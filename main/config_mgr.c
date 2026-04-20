#include "config_mgr.h"
#include "config.h"
 

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

// RAM cache
static char     s_ssid[64];
static char     s_pass[64];
static uint16_t s_port;
static char     s_ip[16];
static bool     s_dhcp_enable;

// Internal helpers
static nvs_handle_t open_nvs(nvs_open_mode_t mode)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, mode, &h);
    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG_CFG, "nvs_open failed: %s", esp_err_to_name(err));
        return 0;
    }
    return h;
}

static void load_str(nvs_handle_t h, const char *key, char *out, size_t max_len, const char *def)
{
    size_t len = max_len;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    if (err != ESP_OK) {
        strncpy(out, def, max_len - 1);
        out[max_len - 1] = '\0';
    }
}

static void load_u16(nvs_handle_t h, const char *key, uint16_t *out, uint16_t def)
{
    esp_err_t err = nvs_get_u16(h, key, out);
    if (err != ESP_OK) {
        *out = def;
    }
}

void config_init(void)
{
    // Init NVS flash partition
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(LOG_TAG_CFG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        nvs_flash_init();
    }

    nvs_handle_t h = open_nvs(NVS_READONLY);
    if (h) {
        load_str(h, NVS_KEY_SSID, s_ssid, sizeof(s_ssid), DEFAULT_AP_SSID);
        load_str(h, NVS_KEY_PASS, s_pass, sizeof(s_pass), DEFAULT_AP_PASS);
        load_u16(h, NVS_KEY_PORT, &s_port, DEFAULT_TCP_PORT);
        load_str(h, NVS_KEY_IP, s_ip, sizeof(s_ip), DEFAULT_AP_IP);
        
        uint8_t dhcp_val = 1; // default on
        if (nvs_get_u8(h, NVS_KEY_DHCP_EN, &dhcp_val) != ESP_OK) {
            s_dhcp_enable = true; 
        } else {
            s_dhcp_enable = (dhcp_val != 0);
        }
        nvs_close(h);
    } else {
        strncpy(s_ssid, DEFAULT_AP_SSID, sizeof(s_ssid) - 1);
        s_ssid[sizeof(s_ssid) - 1] = '\0';

        strncpy(s_pass, DEFAULT_AP_PASS, sizeof(s_pass) - 1);
        s_pass[sizeof(s_pass) - 1] = '\0';

        strncpy(s_ip, DEFAULT_AP_IP, sizeof(s_ip) - 1);
        s_ip[sizeof(s_ip) - 1] = '\0';

        s_port = DEFAULT_TCP_PORT;
        s_dhcp_enable = true;

        ESP_LOGW(LOG_TAG_CFG, "NVS empty, using hardcoded defaults.");
    }

    ESP_LOGI(LOG_TAG_CFG, "Config: SSID=%s, IP=%s, Port=%d, DHCP=%s", 
                s_ssid, s_ip, s_port, s_dhcp_enable ? "ON" : "OFF");
}

const char *config_get_ssid(void) { return s_ssid; }
const char *config_get_pass(void) { return s_pass; }
uint16_t    config_get_port(void) { return s_port; }
const char *config_get_ip(void) { return s_ip; }
bool config_get_dhcp_enable(void) { return s_dhcp_enable; }

void config_set_ssid(const char *ssid)
{
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';

    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (h) {
        nvs_set_str(h, NVS_KEY_SSID, s_ssid);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(LOG_TAG_CFG, "ssid saved: %s", s_ssid);
    }
}

void config_set_pass(const char *pass)
{
    strncpy(s_pass, pass, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';

    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (h) {
        nvs_set_str(h, NVS_KEY_PASS, s_pass);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(LOG_TAG_CFG, "pass saved (hidden)");
    }
}

void config_set_port(uint16_t port)
{
    s_port = port;

    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (h) {
        nvs_set_u16(h, NVS_KEY_PORT, s_port);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(LOG_TAG_CFG, "port saved: %d", s_port);
    }
}
void config_set_ip(const char *ip) {
    strncpy(s_ip, ip, sizeof(s_ip) - 1);
    s_ip[sizeof(s_ip) - 1] = '\0';
    
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (h) {
        nvs_set_str(h, NVS_KEY_IP, s_ip);
        nvs_commit(h);
        nvs_close(h);
    }
}

void config_set_dhcp_enable(bool en) {
    s_dhcp_enable = en;
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (h) {
        nvs_set_u8(h, NVS_KEY_DHCP_EN, (uint8_t)en);
        nvs_commit(h);
        nvs_close(h);
    }
}

void config_dump(void)
{
    ESP_LOGI(LOG_TAG_CFG, "--- Config dump ---");
    ESP_LOGI(LOG_TAG_CFG, "  ssid : %s", s_ssid);
    ESP_LOGI(LOG_TAG_CFG, "  pass : ***");
    ESP_LOGI(LOG_TAG_CFG, "  port : %d", s_port);
    ESP_LOGI(LOG_TAG_CFG, "  ip   : %s", s_ip);
    ESP_LOGI(LOG_TAG_CFG, "  dhcp : %s", s_dhcp_enable ? "ON" : "OFF");
}
