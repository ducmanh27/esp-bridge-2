#pragma once

/**
 * @brief Khởi tạo CLI task đọc từ UART0.
 *        UART0 đã được ESP-IDF init sẵn khi boot (dùng cho logging).
 *        CLI task chỉ đọc thêm, không re-init UART0.
 *
 * Gọi sau tất cả module đã init xong.
 */
void cli_init(void);
