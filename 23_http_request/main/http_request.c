#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <netdb.h>
#include <sys/socket.h>

/* 在menuconfig中不能配置的常量 */
#define WEB_SERVER  "example.com"
#define WEB_PORT    80
#define WEB_URL     "http://example.com/"

static const char *TAG = "example";

static const char *REQUEST = "GET " WEB_URL " HTTP/1.0\r\n"
    "Host: "WEB_SERVER"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

static void http_get_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,           // 协议IPV4
        .ai_socktype = SOCK_STREAM,     // TCP通信
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    while(1) 
    {
        int err = getaddrinfo(WEB_SERVER, "80", &hints, &res);

        if(err != 0 || res == NULL) 
        {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* 打印解析的IP
           注意:inet_ntoa是不可重入的，看看ipaddr_ntoa_r中的“真实”代码 */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        // 创建套节字
        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) 
        {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        // 连接
        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) 
        {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        // 发送HTTP报文
        if (write(s, REQUEST, strlen(REQUEST)) < 0) 
        {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;

        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,  sizeof(receiving_timeout)) < 0) 
        {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        // 读取HTTP响应
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            for(int i = 0; i < r; i++) 
            {
                putchar(recv_buf[i]);
            }
        } while(r > 0);

        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);

        // 关闭socket
        close(s);
        for(int countdown = 10; countdown >= 0; countdown--) 
        {
            ESP_LOGI(TAG, "%d... ", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Starting again!");
    }
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());                  // 初始化默认的NVS分区 
    ESP_ERROR_CHECK(esp_netif_init());                  // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环


    ESP_ERROR_CHECK(example_connect());                 // 配置Wi-Fi或以太网，连接，等待IP

    xTaskCreate(&http_get_task, "http_get_task", 16384, NULL, 5, NULL);
}
