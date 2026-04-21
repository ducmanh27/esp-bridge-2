#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "config.h"
#include <stdint.h>

/**
 * Data chunk truyền giữa các task qua queue.
 */
typedef struct {
    uint8_t  data[QUEUE_CHUNK_SIZE];
    uint16_t len;
} bridge_chunk_t;

// Queue handles (khởi tạo trong bridge_init, dùng bởi cả uart và tcp)
extern QueueHandle_t g_uart_to_tcp_queue;  // uart_rx_task send to tcp_broadcast_task
extern QueueHandle_t g_tcp_to_uart_queue;  // tcp_client_task send to uart_tx_task

/**
 * @brief Tạo queues.
 */
void bridge_init(void);
