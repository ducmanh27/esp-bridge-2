#pragma once

#include <stdint.h>

/**
 * @brief Khởi động TCP server task.
 *        Server lắng nghe trên port từ config_get_port().
 *        Gọi sau wifi_ap_init() và bridge_init().
 */
void tcp_server_init(void);

/**
 * @brief Số client đang kết nối tại thời điểm gọi.
 */
int tcp_server_client_count(void);

/**
 * @brief Gửi bản tin đến tất cả client đang kết nối.
 *        Thread-safe. Dùng nội bộ bởi tcp_broadcast_task.
 * @return Số client nhận thành công.
 */
int tcp_server_broadcast(const uint8_t *data, uint16_t len);
