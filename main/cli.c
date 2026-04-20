#include "cli.h"
#include "config.h"
#include "config_mgr.h"
 
#include "tcp_server.h"
#include "uart_bridge.h"
#include "wifi_ap.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static QueueHandle_t s_cli_uart_event_queue = NULL;

static bool is_valid_ip(const char *ip) {
    int dots = 0;
    int num;
    if (ip == NULL || strlen(ip) < 7 || strlen(ip) > 15) return false;
    
    char tmp[16];
    strcpy(tmp, ip);
    char *tok = strtok(tmp, ".");
    while (tok) {
        for (int i = 0; tok[i]; i++) if (!isdigit((int)tok[i])) return false;
        num = atoi(tok);
        if (num < 0 || num > 255) return false;
        dots++;
        tok = strtok(NULL, ".");
    }
    return dots == 4;
}

static void cli_print(const char *str)
{
    uart_write_bytes(UART0_PORT, str, strlen(str));
}

static void cli_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cli_print(buf);
}

// TODO: manhpd9 Chuyển về dạng cli_table_entry phù hợp cho dễ maintain sau này nếu lượng command cần mở rộng nhiều
// Còn nếu ít command thì dùng if else như hiện tại vẫn phù hợp
// Command handlers

static void cmd_help(void)
{
    cli_print("\r\n");
    cli_print("=== ESP32 UART-WiFi Bridge CLI ===\r\n");
    cli_print("Commands:\r\n");
    // Nhóm lệnh SET
    cli_print("  set ssid <value>    Set WiFi AP SSID\r\n");
    cli_print("  set pass <value>    Set WiFi AP Password (min 8 chars, empty = open)\r\n");
    cli_print("  set port <number>   Set TCP server port\r\n");
    cli_print("  set ip <value>      Set Static IP (e.g., 192.168.4.1)\r\n");
    cli_print("  set dhcp <on|off>   Enable or Disable DHCP Server\r\n");
    
    cli_print("\r\n");
    // Nhóm lệnh GET
    cli_print("  get ssid            Print current SSID\r\n");
    cli_print("  get pass            Print *** (password hidden)\r\n");
    cli_print("  get port            Print current TCP port\r\n");
    cli_print("  get ip              Print current Static IP\r\n");
    cli_print("  get dhcp            Print DHCP Server status (on/off)\r\n");
    cli_print("  get status          Print connection statistics & system heap\r\n");
    cli_print("  get config          Print all stored configuration\r\n");
    
    cli_print("\r\n");
    // Nhóm lệnh hệ thống
    cli_print("  wifi restart        Restart WiFi AP (apply SSID/Pass/IP changes)\r\n");
    cli_print("  reboot              Restart ESP32 (Recommended for IP/DHCP changes)\r\n");
    cli_print("  help                Show this message\r\n");
    cli_print("==================================\r\n");
}

static void cmd_set(const char *key, const char *value)
{
    if (!key || !value || strlen(value) == 0) {
        cli_print("Error: missing value\r\n");
        return;
    }

    if (strcmp(key, "ssid") == 0) {
        if (strlen(value) > 31) {
            cli_print("Error: SSID max 31 chars\r\n");
            return;
        }
        config_set_ssid(value);
        cli_printf("OK — ssid set to \"%s\" (run 'wifi restart' to apply)\r\n", value);

    } else if (strcmp(key, "pass") == 0) {
        if (strlen(value) > 0 && strlen(value) < 8) {
            cli_print("Warning: password < 8 chars — AP will be OPEN after restart\r\n");
        }
        config_set_pass(value);
        cli_print("OK — password saved (run 'wifi restart' to apply)\r\n");

    } else if (strcmp(key, "port") == 0) {
        int port = atoi(value);
        if (port < 1024 || port > 65535) {
            cli_print("Error: port must be 1024–65535\r\n");
            return;
        }
        config_set_port((uint16_t)port);
        cli_printf("OK — port set to %d (take effect after reboot)\r\n", port);

    }
    else if (strcmp(key, "ip") == 0) {
        if (is_valid_ip(value)) {
            config_set_ip(value);
            cli_printf("OK — IP set to %s\r\n", value);
        } else {
            cli_print("Error: Invalid IP format (use x.x.x.x)\r\n");
        }
    }
    else if (strcmp(key, "dhcp") == 0) {
        bool enable = (strcmp(value, "on") == 0);
        config_set_dhcp_enable(enable);
        cli_printf("OK — DHCP Server %s\r\n", enable ? "ENABLED" : "DISABLED");
    }
    else {
        cli_printf("Error: unknown key '%s'\r\n", key);
    }
}

static void cmd_get(const char *key) {
    if (!key) return;

    if (strcmp(key, "ssid") == 0) cli_printf("ssid = %s\r\n", config_get_ssid());
    else if (strcmp(key, "pass") == 0) cli_print("pass = ***\r\n");
    else if (strcmp(key, "port") == 0) cli_printf("port = %d\r\n", config_get_port());
    else if (strcmp(key, "ip") == 0) cli_printf("ip = %s\r\n", config_get_ip());
    else if (strcmp(key, "dhcp") == 0) cli_printf("dhcp = %s\r\n", config_get_dhcp_enable() ? "on" : "off");
    else if (strcmp(key, "status") == 0) {
        cli_printf("TCP clients: %d/%d | UART2 RX: %llu | TX: %llu | Drop: %lu\r\n",
                   tcp_server_client_count(), TCP_MAX_CLIENTS, 
                   uart_bridge_rx_bytes(), uart_bridge_tx_bytes(), 
                   (unsigned long)uart_bridge_dropped_chunks());
        cli_printf("Heap: %lu bytes\r\n", (unsigned long)esp_get_free_heap_size());
    } 
    else if (strcmp(key, "config") == 0) {
        config_dump();
        cli_print("(see log output)\r\n");
    }
    else {
        cli_printf("Error: unknown key '%s'\r\n", key);
    }
}

static void cli_parse(char *line)
{
    // Trim trailing whitespace / CR
    int len = strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == ' ')) {
        line[--len] = '\0';
    }
    if (len == 0) return;

    ESP_LOGI(LOG_TAG_CLI, "CMD: \"%s\"", line);

    // Tokenize (tối đa 3 tokens: verb [noun] [value])
    char *tokens[3] = { NULL, NULL, NULL };
    int   token_count = 0;
    char *ptr = line;
    char *tok;

    while (token_count < 3 && (tok = strtok(ptr, " \t")) != NULL) {
        tokens[token_count++] = tok;
        ptr = NULL;
    }

    if (token_count == 0) return;

    const char *verb  = tokens[0];
    const char *noun  = tokens[1];
    const char *value = tokens[2];

    if (strcmp(verb, "help") == 0 || strcmp(verb, "?") == 0) {
        cmd_help();

    } else if (strcmp(verb, "set") == 0) {
        cmd_set(noun, value);

    } else if (strcmp(verb, "get") == 0) {
        cmd_get(noun);

    } else if (strcmp(verb, "wifi") == 0 && noun && strcmp(noun, "restart") == 0) {
        cli_print("Restarting WiFi AP...\r\n");
        wifi_ap_restart();
        cli_print("WiFi AP restarted.\r\n");

    } else if (strcmp(verb, "reboot") == 0) {
        cli_print("Rebooting in 1 second...\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();

    } else {
        cli_printf("Unknown command: '%s' (type 'help' for list)\r\n", verb);
    }
}

static void cli_task(void *arg)
{
    uart_event_t event;
    char line[CLI_LINE_BUF_SIZE];
    int  line_pos = 0;
    uint8_t read_buf[64]; // Buffer tạm để đọc từ driver

    ESP_LOGI(LOG_TAG_CLI, "CLI Event Task started on UART0");
    cli_print(CLI_PROMPT);

    while (1) {
        if (xQueueReceive(s_cli_uart_event_queue, &event, portMAX_DELAY)) {
            
            switch (event.type) {
                case UART_DATA:
                    int len = uart_read_bytes(UART0_PORT, read_buf, event.size, pdMS_TO_TICKS(10));
                    
                    for (int i = 0; i < len; i++) {
                        uint8_t c = read_buf[i];
                        uart_write_bytes(UART0_PORT, (const char *)&c, 1);

                        if (c == '\r' || c == '\n') {
                            line[line_pos] = '\0';
                            cli_print("\r\n");

                            if (line_pos > 0) {
                                cli_parse(line);
                            }

                            line_pos = 0;
                            cli_print(CLI_PROMPT);

                        } else if (c == 127 || c == '\b') {
                            if (line_pos > 0) {
                                line_pos--;
                                cli_print("\b \b");
                            }
                        } else if (c >= 0x20 && c < 0x7F) {
                            if (line_pos < CLI_LINE_BUF_SIZE - 1) {
                                line[line_pos++] = (char)c;
                            }
                        }
                    }
                    break;

                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    ESP_LOGW(LOG_TAG_CLI, "CLI UART buffer overflow!");
                    uart_flush_input(UART0_PORT);
                    xQueueReset(s_cli_uart_event_queue);
                    break;

                default:
                    break;
            }
        }
    }
}

void cli_init(void)
{
    esp_err_t err = uart_driver_install(UART0_PORT, 
                                        256, 256, 
                                        20, &s_cli_uart_event_queue, 0);
    
    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG_CLI, "Failed to install UART0 driver for CLI");
        return;
    }

    xTaskCreatePinnedToCore(cli_task, "cli_task",
                            TASK_STACK_CLI, NULL,
                            TASK_PRIO_CLI, NULL, 0);
}