/* openSSL server example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <sys/socket.h>

#include "esp_log.h"
#include "openssl/ssl.h"

static const char *TAG = "openssl_server";

extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t ca_pem_end[]   asm("_binary_ca_pem_end");
extern const uint8_t server_pem_start[] asm("_binary_server_pem_start");
extern const uint8_t server_pem_end[]   asm("_binary_server_pem_end");
extern const uint8_t server_key_start[] asm("_binary_server_key_start");
extern const uint8_t server_key_end[]   asm("_binary_server_key_end");

/*
Fragment size range 2048~8192
| Private key len | Fragment size recommend |
| RSA2048         | 2048                    |
| RSA3072         | 3072                    |
| RSA4096         | 4096                    |
*/
#define OPENSSL_SERVER_FRAGMENT_SIZE 2048

/* Local server tcp port */
#define OPENSSL_SERVER_LOCAL_TCP_PORT 443

#define OPENSSL_SERVER_REQUEST "{\"path\": \"/v1/ping/\", \"method\": \"GET\"}\r\n"

/* receive length */
#define OPENSSL_SERVER_RECV_BUF_LEN 1024

static char send_data[] = OPENSSL_SERVER_REQUEST;
static int send_bytes = sizeof(send_data);

static char recv_buf[OPENSSL_SERVER_RECV_BUF_LEN];

static void openssl_server_task(void* p)
{
    int ret;

    SSL_CTX* ctx;
    SSL* ssl;

    struct sockaddr_in sock_addr;
    int sockfd, new_sockfd;
    int recv_bytes = 0;
    socklen_t addr_len;

    ESP_LOGI(TAG, "OpenSSL server thread start...");

    ESP_LOGI(TAG, "create SSL context ......");
    ctx = SSL_CTX_new(TLSv1_2_server_method());
    if (!ctx) {
        ESP_LOGE(TAG, "failed\n");
        goto failed1;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "load server crt ......");
    ret = SSL_CTX_use_certificate_ASN1(ctx, server_pem_end - server_pem_start, server_pem_start);
    if (ret) {
        ESP_LOGI(TAG, "OK\n");
    } 
    else {
        ESP_LOGE(TAG, "failed\n");
        goto failed2;
    }

    ESP_LOGI(TAG, "load server private key ......");
    ret = SSL_CTX_use_PrivateKey_ASN1(0, ctx, server_key_start, server_key_end - server_key_start);
    if (ret) {
        ESP_LOGI(TAG, "OK\n");
    } 
    else {
        ESP_LOGE(TAG, "failed\n");
        goto failed2;
    }

    ESP_LOGI(TAG, "set verify mode verify peer");
    // 客户端将在握手过程中验证从服务器接收到的证书。
    // 这个在wolfSSL中默认打开。
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    ESP_LOGI(TAG, "create socket ......");
    // 创建套节字
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "failed\n");
        goto failed2;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "socket bind ......");
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = 0;
    sock_addr.sin_port = htons(OPENSSL_SERVER_LOCAL_TCP_PORT);
    // 绑定 将IP和端口号与套节字绑定
    ret = bind(sockfd, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
    if (ret) {
        ESP_LOGE(TAG, "failed\n");
        goto failed3;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "server socket listen ......");
    // 监听 同时连接服务器的客户端个数
    ret = listen(sockfd, 32);
    if (ret) {
        ESP_LOGE(TAG, "failed\n");
        goto failed3;
    }
    ESP_LOGI(TAG, "OK\n");

reconnect:
    ESP_LOGI(TAG, "SSL server create ......");
    ssl = SSL_new(ctx);
    if (!ssl) {
        ESP_LOGE(TAG, "failed\n");
        goto failed3;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "SSL server socket accept client ......");
    // 等待客户端连接
    new_sockfd = accept(sockfd, (struct sockaddr*)&sock_addr, &addr_len);
    if (new_sockfd < 0) {
        ESP_LOGE(TAG, "failed\n");
        goto failed4;
    }
    ESP_LOGI(TAG, "OK\n");

    SSL_set_fd(ssl, new_sockfd);

    ESP_LOGI(TAG, "SSL server accept client ......");
    ret = SSL_accept(ssl);
    if (!ret) {
        ESP_LOGE(TAG, "failed\n");
        goto failed5;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "send data to client ......");
    ret = SSL_write(ssl, send_data, send_bytes);
    if (ret <= 0) {
        ESP_LOGE(TAG, "failed, return [-0x%x]\n", -ret);
        goto failed5;
    }
    ESP_LOGI(TAG, "OK\n");

    // 收发数据 
    do {
        ret = SSL_read(ssl, recv_buf, OPENSSL_SERVER_RECV_BUF_LEN - 1);

        if (ret <= 0) {
            break;
        }

        recv_bytes += ret;
        recv_buf[ret] = '\0';
        ESP_LOGI(TAG, "%s", recv_buf);
    } while (1);

    SSL_shutdown(ssl);
failed5:
    close(new_sockfd);
    new_sockfd = -1;
failed4:
    SSL_free(ssl);
    ssl = NULL;
    goto reconnect;
failed3:
    close(sockfd);
    sockfd = -1;
failed2:
    SSL_CTX_free(ctx);
    ctx = NULL;
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

    xTaskCreate(&openssl_server_task, "openssl_server", 8192, NULL, 6, NULL);
}
