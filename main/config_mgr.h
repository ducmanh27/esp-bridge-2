#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Khởi tạo NVS và load config vào RAM cache.
 */
void config_init(void);

// Getters (Trả về giá trị từ RAM cache)
const char *config_get_ssid(void);
const char *config_get_pass(void);
uint16_t    config_get_port(void);
const char *config_get_ip(void);
bool        config_get_dhcp_enable(void);

// Setters (Ghi vào NVS + cập nhật RAM cache)
void config_set_ssid(const char *ssid);
void config_set_pass(const char *pass);
void config_set_port(uint16_t port);
void config_set_ip(const char *ip);
void config_set_dhcp_enable(bool en);

/**
 * @brief In toàn bộ config hiện tại ra logger (không in password rõ).
 */
void config_dump(void);