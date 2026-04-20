#include "tcp_server.h"
#include "bridge.h"
#include "config.h"
#include "config_mgr.h"
 
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/err.h"

#include <string.h>
#include <errno.h>

// ─── Client table ─────────────────────────────────────────────────────────────
typedef struct {
    int      fd;          // socket fd; -1 = slot kosong
    uint32_t ip;          // client IP (network byte order)
    uint16_t port;
} client_slot_t;

static client_slot_t    s_clients[TCP_MAX_CLIENTS];
static SemaphoreHandle_t s_clients_mutex;
static int               s_client_count = 0;

// ─── Internal: cấp / trả slot ─────────────────────────────────────────────────
static int alloc_slot(int fd, uint32_t ip, uint16_t port)
{
    xSemaphoreTake(s_clients_mutex, portMAX_DELAY);
    for (int i = 0; i < TCP_MAX_CLIENTS; i++) {
        if (s_clients[i].fd == -1) {
            s_clients[i].fd   = fd;
            s_clients[i].ip   = ip;
            s_clients[i].port = port;
            s_client_count++;
            xSemaphoreGive(s_clients_mutex);
            return i;
        }
    }
    xSemaphoreGive(s_clients_mutex);
    return -1;  // đầy
}

static void free_slot(int idx)
{
    xSemaphoreTake(s_clients_mutex, portMAX_DELAY);
    if (s_clients[idx].fd != -1) {
        close(s_clients[idx].fd);
        s_clients[idx].fd = -1;
        if (s_client_count > 0) s_client_count--;
    }
    xSemaphoreGive(s_clients_mutex);
}

// Per-client recv task
// Mỗi client kết nối vào sẽ spawn 1 task này.
typedef struct {
    int slot_idx;
} client_task_arg_t;

static void tcp_client_task(void *arg)
{
    client_task_arg_t *a = (client_task_arg_t *)arg;
    int idx = a->slot_idx;
    free(a);

    int fd = s_clients[idx].fd;

    char ip_str[16];
    uint32_t ip_n = s_clients[idx].ip;
    snprintf(ip_str, sizeof(ip_str), "%lu.%lu.%lu.%lu",
             (ip_n >> 0)  & 0xFF,
             (ip_n >> 8)  & 0xFF,
             (ip_n >> 16) & 0xFF,
             (ip_n >> 24) & 0xFF);

    ESP_LOGI(LOG_TAG_TCP, "Client [%d] connected from %s:%d", idx, ip_str, s_clients[idx].port);

    // Set receive timeout để không block mãi mãi
    struct timeval tv = {
        .tv_sec  = 0,
        .tv_usec = 200000,  // 200ms
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    bridge_chunk_t chunk;
    while (1) {
        int n = recv(fd, chunk.data, sizeof(chunk.data), 0);

        if (n > 0) {
            chunk.len = (uint16_t)n;
            ESP_LOGI(LOG_TAG_TCP, "Client [%d] recv %d bytes -> UART2", idx, n);

            BaseType_t ok = xQueueSendToBack(g_tcp_to_uart_queue, &chunk,
                                             pdMS_TO_TICKS(QUEUE_SEND_TIMEOUT_MS));
            if (ok != pdTRUE) {
                ESP_LOGW(LOG_TAG_TCP, "tcp->uart queue full, client [%d] drop %d bytes", idx, n);
            }
        } else if (n == 0) {
            // Client đóng kết nối gracefully
            ESP_LOGI(LOG_TAG_TCP, "Client [%d] disconnected", idx);
            break;
        } else {
            // EAGAIN = timeout (không có dữ liệu), tiếp tục vòng lặp
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            ESP_LOGW(LOG_TAG_TCP, "Client [%d] recv error: %d", idx, errno);
            break;
        }
    }

    free_slot(idx);
    ESP_LOGI(LOG_TAG_TCP, "Client [%d] slot freed (active: %d)", idx, s_client_count);
    vTaskDelete(NULL);
}

// Broadcast task
// Pop g_uart_to_tcp_queue -> gửi đến tất cả client đang kết nối.
static void tcp_broadcast_task(void *arg)
{
    bridge_chunk_t chunk;
    ESP_LOGI(LOG_TAG_TCP, "tcp_broadcast_task started");

    while (1) {
        if (xQueueReceive(g_uart_to_tcp_queue, &chunk, portMAX_DELAY) != pdTRUE) continue;

        ESP_LOGI(LOG_TAG_TCP, "Broadcast %d bytes to %d clients", chunk.len, s_client_count);

        xSemaphoreTake(s_clients_mutex, portMAX_DELAY);
        for (int i = 0; i < TCP_MAX_CLIENTS; i++) {
            if (s_clients[i].fd == -1) continue;

            // Set non-blocking send timeout
            struct timeval tv = {
                .tv_sec  = 0,
                .tv_usec = TCP_SEND_TIMEOUT_MS * 1000,
            };
            setsockopt(s_clients[i].fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            int sent = send(s_clients[i].fd, chunk.data, chunk.len, 0);
            if (sent < 0) {
                ESP_LOGW(LOG_TAG_TCP, "Client [%d] send error %d — marking for close", i, errno);
                // Không close trực tiếp ở đây (tránh race với tcp_client_task).
                // tcp_client_task sẽ phát hiện lỗi và tự close.
                // Nếu muốn force close: shutdown(s_clients[i].fd, SHUT_RDWR);
                shutdown(s_clients[i].fd, SHUT_RDWR);
            }
        }
        xSemaphoreGive(s_clients_mutex);
    }
}

// ─── Server accept task ───────────────────────────────────────────────────────
static void tcp_server_task(void *arg)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0) {
        ESP_LOGE(LOG_TAG_TCP, "socket() failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(config_get_port()),
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(LOG_TAG_TCP, "bind() failed: %d", errno);
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(server_fd, TCP_MAX_CLIENTS) < 0) {
        ESP_LOGE(LOG_TAG_TCP, "listen() failed: %d", errno);
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(LOG_TAG_TCP, "TCP server listening on port %d", config_get_port());

    // Tạo broadcast task (Core 1 để gần UART)
    xTaskCreatePinnedToCore(tcp_broadcast_task, "tcp_bcast_task",
                            TASK_STACK_BROADCAST, NULL,
                            TASK_PRIO_BROADCAST, NULL, 1);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t          client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            ESP_LOGW(LOG_TAG_TCP, "accept() error: %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (s_client_count >= TCP_MAX_CLIENTS) {
            ESP_LOGW(LOG_TAG_TCP, "Max clients reached (%d), rejecting connection", TCP_MAX_CLIENTS);
            close(client_fd);
            continue;
        }

        int slot = alloc_slot(client_fd,
                              client_addr.sin_addr.s_addr,
                              ntohs(client_addr.sin_port));
        if (slot < 0) {
            ESP_LOGE(LOG_TAG_TCP, "No free slot (should not happen)");
            close(client_fd);
            continue;
        }

        // Tạo task riêng cho client
        client_task_arg_t *targ = malloc(sizeof(client_task_arg_t));
        if (!targ) {
            ESP_LOGE(LOG_TAG_TCP, "malloc failed for client task arg");
            free_slot(slot);
            continue;
        }
        targ->slot_idx = slot;

        char task_name[24];
        snprintf(task_name, sizeof(task_name), "tcp_client_%d", slot);

        xTaskCreatePinnedToCore(tcp_client_task, task_name,
                                TASK_STACK_TCP_CLIENT, targ,
                                TASK_PRIO_TCP_CLIENT, NULL, 0);
    }
}

void tcp_server_init(void)
{
    s_clients_mutex = xSemaphoreCreateMutex();
    for (int i = 0; i < TCP_MAX_CLIENTS; i++) {
        s_clients[i].fd = -1;
    }

    xTaskCreatePinnedToCore(tcp_server_task, "tcp_server_task",
                            TASK_STACK_TCP_SERVER, NULL,
                            TASK_PRIO_TCP_SERVER, NULL, 0);
}

int tcp_server_client_count(void)
{
    return s_client_count;
}

int tcp_server_broadcast(const uint8_t *data, uint16_t len)
{
    // Hàm này không dùng trực tiếp (broadcast qua queue),
    // nhưng expose để CLI có thể gọi kiểm tra.
    int ok = 0;
    xSemaphoreTake(s_clients_mutex, portMAX_DELAY);
    for (int i = 0; i < TCP_MAX_CLIENTS; i++) {
        if (s_clients[i].fd == -1) continue;
        if (send(s_clients[i].fd, data, len, 0) > 0) ok++;
    }
    xSemaphoreGive(s_clients_mutex);
    return ok;
}
