#include "bridge.h"
#include "esp_log.h"
#include "config.h"
#include "esp_log.h"
static const char *TAG = "BRIDGE";

QueueHandle_t g_uart_to_tcp_queue = NULL;
QueueHandle_t g_tcp_to_uart_queue = NULL;

void bridge_init(void)
{
    g_uart_to_tcp_queue = xQueueCreate(QUEUE_UART_TO_TCP_LEN, sizeof(bridge_chunk_t));
    g_tcp_to_uart_queue = xQueueCreate(QUEUE_TCP_TO_UART_LEN, sizeof(bridge_chunk_t));

    if (!g_uart_to_tcp_queue || !g_tcp_to_uart_queue) {
        ESP_LOGE(TAG, "Queue creation failed! Insufficient heap.");
        esp_restart();
    }

    ESP_LOGI(TAG, "Queues created (uart->tcp: %d items, tcp->uart: %d items)",
         QUEUE_UART_TO_TCP_LEN, QUEUE_TCP_TO_UART_LEN);
}
