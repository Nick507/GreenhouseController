#include "wifi_net.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "configuration.h"
#include "credentials.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT    BIT0
#define TCP_FLUSH_MS          300
#define TCP_RECV_TIMEOUT_SEC  5

static EventGroupHandle_t s_wifi_event_group;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static int s_sock = -1;
static bool s_shutting_down = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (s_shutting_down) {
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "disconnected, reconnecting");
        if (s_sock >= 0) {
            close(s_sock);
            s_sock = -1;
        }
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t send_all(int sock, const void *data, size_t len)
{
    const char *p = data;
    while (len > 0) {
        int sent = send(sock, p, len, 0);
        if (sent < 0) {
            ESP_LOGW(TAG, "send errno %d", errno);
            return ESP_FAIL;
        }
        p += sent;
        len -= (size_t)sent;
    }
    return ESP_OK;
}

static void tcp_socket_configure(int sock)
{
    struct linger ling = {.l_onoff = 1, .l_linger = 2};
    setsockopt(sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));

    struct timeval tv = {.tv_sec = TCP_RECV_TIMEOUT_SEC, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

static void tcp_close_graceful(void)
{
    if (s_sock < 0) {
        return;
    }

    shutdown(s_sock, SHUT_WR);
    vTaskDelay(pdMS_TO_TICKS(TCP_FLUSH_MS));
    close(s_sock);
    s_sock = -1;
    ESP_LOGI(TAG, "TCP closed");
}

esp_err_t wifi_net_init(void)
{
    s_shutting_down = false;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_wifi_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &s_ip_handler));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_ESP_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_ESP_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to \"%s\"...", CONFIG_ESP_WIFI_SSID);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "TCP target %s:%d", CONFIG_TARGET_IP, CONFIG_TARGET_PORT);
    return ESP_OK;
}

static esp_err_t tcp_connect(void)
{
    if (s_sock >= 0) {
        return ESP_OK;
    }

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_TARGET_PORT),
    };
    if (inet_pton(AF_INET, CONFIG_TARGET_IP, &dest.sin_addr) != 1) {
        ESP_LOGE(TAG, "invalid IP: %s", CONFIG_TARGET_IP);
        return ESP_ERR_INVALID_ARG;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed: errno %d", errno);
        return ESP_FAIL;
    }

    tcp_socket_configure(sock);

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGE(TAG, "connect to %s:%d failed: errno %d",
                 CONFIG_TARGET_IP, CONFIG_TARGET_PORT, errno);
        close(sock);
        return ESP_FAIL;
    }

    s_sock = sock;
    ESP_LOGI(TAG, "TCP connected");
    return ESP_OK;
}

esp_err_t wifi_net_exchange(const char *request, char *response, size_t response_size, size_t *response_len)
{
    if (request == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) == 0) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    esp_err_t err = tcp_connect();
    if (err != ESP_OK) {
        return err;
    }

    size_t req_len = strlen(request);
    char newline = '\n';
    err = send_all(s_sock, request, req_len);
    if (err == ESP_OK) {
        err = send_all(s_sock, &newline, 1);
    }
    if (err != ESP_OK) {
        close(s_sock);
        s_sock = -1;
        return err;
    }

    if (response != NULL && response_size > 0 && response_len != NULL) {
        size_t total = 0;
        while (total + 1 < response_size) {
            int n = recv(s_sock, response + total, response_size - total - 1, 0);
            if (n < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    err = ESP_ERR_TIMEOUT;
                } else {
                    err = ESP_FAIL;
                }
                break;
            }
            if (n == 0) {
                break;
            }

            for (int i = 0; i < n; i++) {
                if (response[total + (size_t)i] == '\n') {
                    total += (size_t)i;
                    response[total] = '\0';
                    *response_len = total;
                    tcp_close_graceful();
                    return ESP_OK;
                }
            }

            total += (size_t)n;
        }

        response[total] = '\0';
        *response_len = total;
        if (err == ESP_OK && total == 0) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    tcp_close_graceful();
    return err;
}

void wifi_net_deinit(void)
{
    s_shutting_down = true;

    if (s_sock >= 0) {
        tcp_close_graceful();
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    if (s_wifi_handler) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        s_wifi_handler = NULL;
    }
    if (s_ip_handler) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        s_ip_handler = NULL;
    }

    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_wifi_deinit();

    ESP_LOGI(TAG, "WiFi stopped");
}
