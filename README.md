# ESP32 UART-WiFi Bridge Firmware

Firmware tự viết thay thế AT-command, cung cấp:
- **Soft AP** — ESP32 phát WiFi, các thiết bị khác kết nối vào
- **TCP Server** — nhận tối đa 4 TCP client đồng thời
- **UART2 Bridge** — 2 chiều: UART2 RX → broadcast TCP, TCP recv → UART2 TX
- **CLI** — cấu hình ssid/pass/port qua UART0
- **Logger** — ghi event ra UART0 (cổng flash)

---

## Cấu trúc file

```
esp32_uart_wifi_bridge/
├── CMakeLists.txt
├── sdkconfig.defaults
├── test_loopback.py          ← test script chạy từ PC
└── main/
    ├── CMakeLists.txt
    ├── config.h              ← tất cả constants / defaults
    ├── logger.h / logger.c   ← ring buffer async logger
    ├── config_mgr.h / .c     ← NVS read/write + RAM cache
    ├── bridge.h / bridge.c   ← FreeRTOS queue init
    ├── uart_bridge.h / .c    ← UART2 RX+TX tasks
    ├── wifi_ap.h / wifi_ap.c ← Soft AP + event handler
    ├── tcp_server.h / .c     ← TCP server + per-client task + broadcast
    ├── cli.h / cli.c         ← UART0 CLI parser
    └── main.c                ← boot sequence
```

---

## Hardware

| Chức năng | GPIO | UART |
|-----------|------|------|
| Flash / Log / CLI | GPIO1 (TX), GPIO3 (RX) | UART0 |
| Giao tiếp Host MCU | GPIO17 (TX), GPIO16 (RX) | UART2 |

---

## Build & Flash

```bash
# Cài ESP-IDF v5.x
. $IDF_PATH/export.sh

cd esp32_uart_wifi_bridge
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## CLI Commands

Kết nối terminal (115200 baud) vào UART0:

```
set ssid <value>      Đặt SSID (tối đa 31 ký tự)
set pass <value>      Đặt password (tối thiểu 8 ký tự; để trống = Open AP)
set port <number>     Đặt TCP port (1024–65535)
get ssid              Xem SSID hiện tại
get pass              Xem password (hiển thị ***)
get port              Xem TCP port
get status            Xem số client kết nối, thống kê UART, heap free
get config            Xem toàn bộ config
wifi restart          Áp dụng ssid/pass mới (restart WiFi, không reboot)
reboot                Khởi động lại ESP32
help                  Danh sách lệnh
```

---

## Mặc định

| Thông số | Giá trị mặc định |
|----------|-----------------|
| SSID | `ESP32-Bridge` |
| Password | `12345678` |
| TCP Port | `8080` |
| AP IP | `192.168.4.1` |
| Max TCP clients | 4 |
| UART2 baud | 115200 |

---

## Chạy Test

```bash
# Kết nối PC vào WiFi ESP32-Bridge, sau đó:
pip install pyserial
python test_loopback.py --host 192.168.4.1 --port 8080 --serial /dev/ttyUSB0

# Chạy test cụ thể (ví dụ test 1 và 3):
python test_loopback.py --tests 1,3
```

---

## Thiết kế Task / Core

| Task | Core | Priority | Lý do |
|------|------|----------|-------|
| `tcp_server_task` | 0 | 5 | WiFi stack dùng Core 0 |
| `tcp_client_task[N]` | 0 | 4 | Gần WiFi stack |
| `tcp_bcast_task` | 1 | 4 | Gần UART |
| `uart_rx_task` | 1 | 6 | Ưu tiên cao nhất — không được miss byte |
| `uart_tx_task` | 1 | 5 | Ghi UART nhanh |
| `cli_task` | 0 | 3 | Không time-critical |
| `logger_task` | 0 | 2 | Thấp nhất — background flush |

---

## Lưu ý

- **UART0 dùng chung CLI và Logger**: prefix log là `[ms][L][TAG]`, prefix CLI là `> `.  
  Nếu log quá nhiều ảnh hưởng CLI, giảm log level: `logger_set_level(LOG_LEVEL_WARN)`.
- **Port thay đổi** chỉ có hiệu lực sau `reboot` (TCP server bind port lúc khởi động).
- **SSID/Pass thay đổi** có hiệu lực ngay sau `wifi restart` (không cần reboot).
- **Dòng điện**: đảm bảo nguồn ≥ 500mA, capacitor decoupling 100µF gần chân 3V3.
