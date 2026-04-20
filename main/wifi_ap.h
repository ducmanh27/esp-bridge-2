#pragma once

/**
 * @brief Khởi tạo WiFi stack và bật Soft AP.
 *        SSID/password lấy từ config_mgr.
 *        Block cho đến khi AP sẵn sàng (IP đã assign).
 */
void wifi_ap_init(void);

/**
 * @brief Dừng AP, dọn dẹp WiFi stack (dùng trước khi restart sau set ssid/pass).
 */
void wifi_ap_stop(void);

/**
 * @brief Restart AP với config mới (stop + start).
 */
void wifi_ap_restart(void);
