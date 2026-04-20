#include "uart_bridge.h"
#include "bridge.h"
#include "config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include <string.h>
#include <stdint.h>

static const char *TAG = "UART";

static QueueHandle_t s_uart_event_queue;

static volatile uint64_t s_rx_bytes = 0;
static volatile uint64_t s_tx_bytes = 0;
static volatile uint32_t s_dropped  = 0;

// UART2 RX Task
// Block chờ uart_event_t từ driver. Driver ISR tự push event khi:
//   UART_DATA        → có data mới trong ring buffer
//   UART_FIFO_OVF    → FIFO overflow (baud quá cao hoặc task xử lý chậm)
//   UART_BUFFER_FULL → ring buffer đầy
//   UART_FRAME_ERR   → lỗi framing (sai baud/format)
static void uart_rx_task(void *arg)
{
    uart_event_t   event;
    bridge_chunk_t chunk;

    ESP_LOGI(TAG, "uart_rx_task started (event-driven)");

    while (1) {
        if (xQueueReceive(s_uart_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (event.type) {

        case UART_DATA: {
            uint32_t remaining = event.size;
            while (remaining > 0) {
                uint32_t to_read = remaining > sizeof(chunk.data)
                                   ? sizeof(chunk.data)
                                   : remaining;

                int num_bytes = uart_read_bytes(UART2_PORT, chunk.data, to_read, 0);
                if (num_bytes <= 0) break;

                chunk.len   = (uint16_t)num_bytes;
                s_rx_bytes += num_bytes;
                remaining  -= (uint32_t)num_bytes;
                BaseType_t ok = xQueueSendToBack(g_uart_to_tcp_queue, &chunk,
                                                 pdMS_TO_TICKS(QUEUE_SEND_TIMEOUT_MS));
                if (ok != pdTRUE) {
                    s_dropped++;
                    ESP_LOGW(TAG, "uart->tcp queue full, dropped %d bytes (total: %lu)",
                             num_bytes, (unsigned long)s_dropped);
                }
            }
            break;
        }

        case UART_FIFO_OVF:
            // Hardware FIFO tràn — thường do baud cao + task bị preempt lâu
            // Tăng TASK_PRIO_UART_RX hoặc giảm baud nếu hay xảy ra
            ESP_LOGW(TAG, "UART FIFO overflow — flush rx");
            uart_flush_input(UART2_PORT);
            xQueueReset(s_uart_event_queue);
            break;

        case UART_BUFFER_FULL:
            // Software ring buffer của driver đầy
            // Tăng UART2_BUF_SIZE trong config.h nếu hay xảy ra
            ESP_LOGW(TAG, "UART ring buffer full — flush rx");
            uart_flush_input(UART2_PORT);
            xQueueReset(s_uart_event_queue);
            break;

        case UART_FRAME_ERR:
            ESP_LOGE(TAG, "UART frame error — check baud rate / wiring");
            break;

        case UART_PARITY_ERR:
            ESP_LOGE(TAG, "UART parity error");
            break;

        default:
            ESP_LOGI(TAG, "UART unhandled event type: %d", event.type);
            break;
        }
    }
}

// Pop g_tcp_to_uart_queue → ghi ra UART2 TX.
static void uart_tx_task(void *arg)
{
    bridge_chunk_t chunk;

    ESP_LOGI(TAG, "uart_tx_task started");

    while (1) {
        if (xQueueReceive(g_tcp_to_uart_queue, &chunk, portMAX_DELAY) == pdTRUE) {
            int written = uart_write_bytes(UART2_PORT,
                                           (const char *)chunk.data,
                                           chunk.len);
            if (written < 0) {
                ESP_LOGE(TAG, "uart_write_bytes error");
            } else {
                s_tx_bytes += (uint32_t)written;
                ESP_LOGI(TAG, "TX %d bytes to UART2", written);
            }
        }
    }
}

void  uart_bridge_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = UART2_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err;

    err = uart_driver_install(UART2_PORT,
                              UART2_BUF_SIZE * 2,  
                              UART2_BUF_SIZE,      
                              20,                  
                              &s_uart_event_queue,  
                              0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return;
    }

    err = uart_param_config(UART2_PORT, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return;
    }

    err = uart_set_pin(UART2_PORT,
                       UART2_TX_PIN, UART2_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return;
    }

    // Ngưỡng UART_DATA event: driver chỉ push event khi có >= N bytes
    // = 1 -> event ngay khi có 1 byte (latency thấp nhất)
    // Tăng lên 64-128 nếu cần throughput cao hơn latency
    uart_set_rx_full_threshold(UART2_PORT, 20);

    // RX timeout: flush event sau N ký tự bit-time không có data mới
    // Đảm bảo packet cuối không bị kẹt khi lưu lượng thưa
    uart_set_rx_timeout(UART2_PORT, 10);

    ESP_LOGI(TAG, "UART2 init OK — event-driven (baud=%d TX=%d RX=%d)",
             UART2_BAUD, UART2_TX_PIN, UART2_RX_PIN);

    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx_task",
                            TASK_STACK_UART_RX, NULL,
                            TASK_PRIO_UART_RX, NULL, 1);

    xTaskCreatePinnedToCore(uart_tx_task, "uart_tx_task",
                            TASK_STACK_UART_TX, NULL,
                            TASK_PRIO_UART_TX, NULL, 1);
}

uint64_t uart_bridge_rx_bytes(void)       { return s_rx_bytes; }
uint64_t uart_bridge_tx_bytes(void)       { return s_tx_bytes; }
uint32_t uart_bridge_dropped_chunks(void) { return s_dropped;  }