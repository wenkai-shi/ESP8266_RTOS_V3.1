/* openSSL demo example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "openssl/ssl.h"
#include "protocol_examples_common.h"

static const char *TAG = "example";

#define OPENSSL_DEMO_LOCAL_TCP_PORT     9999

#define OPENSSL_DEMO_TARGET_NAME        "www.baidu.com"
#define OPENSSL_DEMO_TARGET_TCP_PORT    443

#define OPENSSL_DEMO_REQUEST            "GET / HTTP/1.1\r\n\r\n"

#define OPENSSL_DEMO_RECV_BUF_LEN       1024

static char send_data[] = OPENSSL_DEMO_REQUEST;
static int send_bytes = sizeof(send_data);

static char recv_buf[OPENSSL_DEMO_RECV_BUF_LEN];

static void openssl_task(void *p)
{
    int ret;

    SSL_CTX *ctx;
    SSL *ssl;

    int sockfd;
    struct sockaddr_in sock_addr;       // 存储server地址结构
    struct hostent *entry = NULL;

    int recv_bytes = 0;

    ESP_LOGI(TAG, "OpenSSL demo thread start...");

    // 获取主机名的addr信息
    do {
        entry = gethostbyname(OPENSSL_DEMO_TARGET_NAME);
        vTaskDelay(500 / portTICK_RATE_MS);
    } while (entry == NULL);

    ESP_LOGI(TAG, "get target IP is %d.%d.%d.%d", (unsigned char)((((struct in_addr *)(entry->h_addr))->s_addr & 0x000000ff) >> 0),
                                                  (unsigned char)((((struct in_addr *)(entry->h_addr))->s_addr & 0x0000ff00) >> 8),
                                                  (unsigned char)((((struct in_addr *)(entry->h_addr))->s_addr & 0x00ff0000) >> 16),
                                                  (unsigned char)((((struct in_addr *)(entry->h_addr))->s_addr & 0xff000000) >> 24));

    ESP_LOGI(TAG, "create SSL context ......");
    ctx = SSL_CTX_new(TLSv1_1_client_method());
    if (!ctx) {
        ESP_LOGE(TAG, "failed\n");
        goto failed1;
    }
    ESP_LOGI(TAG, "OK\n");

    // 客户端将在握手过程中验证从服务器接收到的证书。
    // 这个在wolfSSL中默认打开。
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    ESP_LOGI(TAG, "create socket ......");
    // 创建套节字
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "failed\n");
        goto failed2;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "bind socket ......");
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = 0;
    sock_addr.sin_port = htons(OPENSSL_DEMO_LOCAL_TCP_PORT);
    // 绑定 将IP和端口号与套节字绑定
    ret = bind(sockfd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    if (ret) {
        ESP_LOGE(TAG, "failed\n");
        goto failed3;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "socket connect to remote ......");
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = ((struct in_addr *)(entry->h_addr))->s_addr;
    sock_addr.sin_port = htons(OPENSSL_DEMO_TARGET_TCP_PORT);
    // 连接服务器
    ret = connect(sockfd, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    if (ret) {
        ESP_LOGE(TAG, "failed\n");
        goto failed4;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "create SSL ......");
    ssl = SSL_new(ctx);
    if (!ssl) {
        ESP_LOGE(TAG, "failed\n");
        goto failed5;
    }
    ESP_LOGI(TAG, "OK\n");

    SSL_set_fd(ssl, sockfd);

    ESP_LOGI(TAG, "SSL connected to %s port %d ......", OPENSSL_DEMO_TARGET_NAME, OPENSSL_DEMO_TARGET_TCP_PORT);
    ret = SSL_connect(ssl);
    if (!ret) {
        ESP_LOGE(TAG, "SSL_connect failed, return [-0x%x]\n", -ret);
        goto failed6;
    }

    ESP_LOGI(TAG, "OK\n");

#if configENABLE_TASK_MODIFY_STACK_DEPTH == 1
    ESP_LOGI(TAG, "Before modification stack depth of this thread, free heap size is %u", esp_get_free_heap_size());
    vTaskModifyStackDepth(NULL, 2048);
    ESP_LOGI(TAG, "After modification stack depth of this thread, free heap size is %u", esp_get_free_heap_size());
#endif

    ESP_LOGI(TAG,  "send request to %s port %d ......", OPENSSL_DEMO_TARGET_NAME, OPENSSL_DEMO_TARGET_TCP_PORT);
    ret = SSL_write(ssl, send_data, send_bytes);

    if (ret <= 0) {
        ESP_LOGE(TAG, "failed, return [-0x%x]\n", -ret);
        goto failed7;
    }

    ESP_LOGI(TAG, "OK\n");

    // 收发数据 
    do {
        ret = SSL_read(ssl, recv_buf, OPENSSL_DEMO_RECV_BUF_LEN - 1);

        if (ret <= 0) {
            ESP_LOGE(TAG, "SSL_read failed\n");
            break;
        }

        recv_bytes += ret;
        ESP_LOGI(TAG, "%s", recv_buf);
    } while (1);

    ESP_LOGI(TAG, "read %d bytes data from %s ......", recv_bytes, OPENSSL_DEMO_TARGET_NAME);

failed7:
    SSL_shutdown(ssl);
failed6:
    SSL_free(ssl);
failed5:
failed4:
failed3:
    close(sockfd);
failed2:
    SSL_CTX_free(ctx);
failed1:
    vTaskDelete(NULL);

    ESP_LOGI(TAG, "task exit");

    return ;
}

void app_main(void)
{
    ESP_LOGI(TAG, "\n\n-----------------------Program starts-----------------------");

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size()); // 获取芯片可用内存
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());          // 返回完整的IDF版本字符串


    ESP_ERROR_CHECK(nvs_flash_init());                      // 初始化默认的NVS分区 
    ESP_ERROR_CHECK(esp_netif_init());                      // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());       // 创建默认事件循环

    ESP_ERROR_CHECK(example_connect());                     // 配置Wi-Fi或以太网，连接，等待IP

    xTaskCreate(&openssl_task, "openssl_task", 8192, NULL, 5, NULL);
}
