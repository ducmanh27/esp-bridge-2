#pragma once
#include <stdint.h>
/**
 * @brief Khởi tạo UART2 và tạo 2 tasks:
 *   - uart_rx_task : đọc UART2, push vào g_uart_to_tcp_queue
 *   - uart_tx_task : pop g_tcp_to_uart_queue, ghi UART2
 *
 * Gọi sau bridge_init().
 */
void uart_bridge_init(void);

/**
 * @brief Trả về số byte đã nhận từ UART2 kể từ boot (cho CLI status).
 */
uint64_t uart_bridge_rx_bytes(void);

/**
 * @brief Trả về số byte đã gửi ra UART2 kể từ boot.
 */
uint64_t uart_bridge_tx_bytes(void);

/**
 * @brief Số chunk bị drop do queue đầy.
 */
uint32_t uart_bridge_dropped_chunks(void);
