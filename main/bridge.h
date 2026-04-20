#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "config.h"
#include <stdint.h>

/**
 * Data chunk truyền giữa các task qua queue.
 * Kích thước cố định để tránh dynamic allocation.
 */
typedef struct {
    uint8_t  data[QUEUE_CHUNK_SIZE];
    uint16_t len;
} bridge_chunk_t;

// ─── Queue handles (khởi tạo trong bridge_init, dùng bởi cả uart và tcp) ─────
extern QueueHandle_t g_uart_to_tcp_queue;  // uart_rx_task → tcp_broadcast_task
extern QueueHandle_t g_tcp_to_uart_queue;  // tcp_client_task → uart_tx_task

/**
 * @brief Tạo queues. Gọi trước khi tạo bất kỳ task nào.
 */
void bridge_init(void);
