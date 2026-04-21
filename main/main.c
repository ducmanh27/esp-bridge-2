 
#include "config_mgr.h"
#include "bridge.h"
#include "uart_bridge.h"
#include "wifi_ap.h"
#include "tcp_server.h"
#include "cli.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

void app_main(void)
{
    ESP_LOGI(LOG_TAG_MAIN, "=== ESP32 UART-WiFi Bridge Booting ===");
    ESP_LOGI(LOG_TAG_MAIN, "ESP-IDF version: %s", esp_get_idf_version());
    ESP_LOGI(LOG_TAG_MAIN, "Free heap at boot: %lu bytes", (unsigned long)esp_get_free_heap_size());

    config_init();

    config_dump();

    bridge_init();

    uart_bridge_init();

    wifi_ap_init();

    tcp_server_init();

    cli_init();

    ESP_LOGI(LOG_TAG_MAIN, "=== Boot complete ===");
    ESP_LOGI(LOG_TAG_MAIN, "SSID: %s  |  TCP Port: %d  |  Max clients: %d",
         config_get_ssid(), config_get_port(), TCP_MAX_CLIENTS);
}
