#pragma once

// ─── WiFi Soft AP ────────────────────────────────────────────────────────────
#define DEFAULT_AP_SSID        "ESP32-Bridge"
#define DEFAULT_AP_PASS        "12345678"
#define AP_MAX_CONNECTIONS     2
#define AP_CHANNEL             1
#define AP_BEACON_INTERVAL_MS  100
#define DEFAULT_AP_IP          "192.168.4.1"
#define DEFAULT_AP_GW          "192.168.4.1"
#define DEFAULT_AP_NETMASK     "255.255.255.0"


// ─── TCP Server ───────────────────────────────────────────────────────────────
#define DEFAULT_TCP_PORT       8080
#define TCP_MAX_CLIENTS        2
#define TCP_RECV_BUF_SIZE      1024
#define TCP_SEND_TIMEOUT_MS    200
#define TCP_RECV_TIMEOUT_MS    0       // 0 = blocking (handled by select)
#define TCP_SELECT_TIMEOUT_MS  100

// ─── UART2 (giao tiếp với host MCU) ─────────────────────────────────────────
#define UART2_PORT             UART_NUM_2
#define UART2_BAUD             115200
#define UART2_TX_PIN           17
#define UART2_RX_PIN           16
#define UART2_BUF_SIZE         2048
#define UART2_PATTERN_TIMEOUT  10      // ticks

// ─── UART0 (CLI + logging, cổng flash) ───────────────────────────────────────
#define UART0_PORT             UART_NUM_0
#define UART0_BAUD             115200
#define CLI_LINE_BUF_SIZE      128
#define CLI_PROMPT             "\r\n> "

// ─── FreeRTOS Queues ─────────────────────────────────────────────────────────
#define QUEUE_UART_TO_TCP_LEN  32      // số lượng items (mỗi item 1 chunk)
#define QUEUE_TCP_TO_UART_LEN  32
#define QUEUE_CHUNK_SIZE       512     // bytes mỗi item
#define QUEUE_SEND_TIMEOUT_MS  5       // drop nếu queue đầy sau 5ms

// ─── FreeRTOS Tasks ──────────────────────────────────────────────────────────
#define TASK_STACK_TCP_SERVER  4096
#define TASK_STACK_TCP_CLIENT  3072
#define TASK_STACK_UART_RX     4096
#define TASK_STACK_UART_TX     4096
#define TASK_STACK_BROADCAST   3072
#define TASK_STACK_CLI         4096

#define TASK_PRIO_TCP_SERVER   5
#define TASK_PRIO_TCP_CLIENT   4
#define TASK_PRIO_UART_RX      6
#define TASK_PRIO_UART_TX      5
#define TASK_PRIO_BROADCAST    4
#define TASK_PRIO_CLI          3
#define TASK_PRIO_LOGGER       2

// ─── NVS Keys ────────────────────────────────────────────────────────────────
#define NVS_NAMESPACE          "bridge_cfg"
#define NVS_KEY_SSID           "ssid"
#define NVS_KEY_PASS           "pass"
#define NVS_KEY_PORT           "port"
#define NVS_KEY_IP             "ap_ip"
#define NVS_KEY_DHCP_EN        "dhcp_en"

// ─── Logger ───────────────────────────────────────────────────────────────────
#define LOG_RING_BUF_SIZE      2048
#define LOG_TAG_MAIN           "MAIN"
#define LOG_TAG_WIFI           "WIFI"
#define LOG_TAG_TCP            "TCP"
#define LOG_TAG_UART           "UART"
#define LOG_TAG_CLI            "CLI"
#define LOG_TAG_CFG            "CFG"

