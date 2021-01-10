/* openSSL client example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

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
#include <netdb.h>

#include "esp_log.h"
#include "openssl/ssl.h"

static const char *TAG = "openssl_client";

extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t ca_pem_end[]   asm("_binary_ca_pem_end");
extern const uint8_t client_pem_start[] asm("_binary_client_pem_start");
extern const uint8_t client_pem_end[]   asm("_binary_client_pem_end");
extern const uint8_t client_key_start[] asm("_binary_client_key_start");
extern const uint8_t client_key_end[]   asm("_binary_client_key_end");

/*
Fragment size range 2048~8192
| Private key len | Fragment size recommend |
| RSA2048         | 2048                    |
| RSA3072         | 3072                    |
| RSA4096         | 4096                    |
*/
#define OPENSSL_CLIENT_FRAGMENT_SIZE 2048

/* 当地的tcp端口 */
#define OPENSSL_CLIENT_LOCAL_TCP_PORT 1000

#define OPENSSL_CLIENT_REQUEST "{\"path\": \"/v1/ping/\", \"method\": \"GET\"}\r\n"

/* 接收长度 */
#define OPENSSL_CLIENT_RECV_BUF_LEN 1024

static char send_data[] = OPENSSL_CLIENT_REQUEST;
static int send_bytes = sizeof(send_data);

static char recv_buf[OPENSSL_CLIENT_RECV_BUF_LEN];

static void openssl_client_task(void* p)
{
    int ret;

    SSL_CTX* ctx;
    SSL* ssl;

    int sockfd;
    struct sockaddr_in sock_addr;
    struct hostent* entry = NULL;
    int recv_bytes = 0;

    ESP_LOGI(TAG, "OpenSSL client thread start...");

    // 获取主机名的addr信息
    do {
        entry = gethostbyname(CONFIG_TARGET_DOMAIN);
        vTaskDelay(100 / portTICK_RATE_MS);
    } while (entry == NULL);

    ESP_LOGI(TAG, "create SSL context ......");
    ctx = SSL_CTX_new(TLSv1_2_client_method());
    if (!ctx) {
        ESP_LOGE(TAG, "failed\n");
        goto failed1;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "load client crt ......");
    ret = SSL_CTX_use_certificate_ASN1(ctx, client_pem_end - client_pem_start, client_pem_start);
    if (ret) {
        ESP_LOGI(TAG, "OK\n");
    } 
    else {
        ESP_LOGE(TAG, "failed\n");
        goto failed2;
    }

    ESP_LOGI(TAG, "load client private key ......");
    ret = SSL_CTX_use_PrivateKey_ASN1(0, ctx, client_key_start, client_key_end - client_key_start);
    if (ret) {
        ESP_LOGI(TAG, "OK\n");
    } 
    else {
        ESP_LOGE(TAG, "failed\n");
        goto failed2;
    }

    ESP_LOGI(TAG, "set verify mode verify peer");
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    ESP_LOGI(TAG, "create socket ......");
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        ESP_LOGE(TAG, "failed\n");
        goto failed3;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "bind socket ......");
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = 0;
    sock_addr.sin_port = htons(OPENSSL_CLIENT_LOCAL_TCP_PORT);
    ret = bind(sockfd, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
    if (ret) {
        ESP_LOGE(TAG, "failed\n");
        goto failed4;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "socket connect to remote ......");
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = ((struct in_addr*)(entry->h_addr))->s_addr;
    sock_addr.sin_port = htons(CONFIG_TARGET_PORT_NUMBER);
    ret = connect(sockfd, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
    if (ret) {
        ESP_LOGE(TAG, "failed\n");
        goto failed5;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "create SSL ......");
    ssl = SSL_new(ctx);
    if (!ssl) {
        ESP_LOGE(TAG, "failed\n");
        goto failed6;
    }
    ESP_LOGI(TAG, "OK\n");

    SSL_set_fd(ssl, sockfd);

    ESP_LOGI(TAG, "SSL connected to %s port %d ......", CONFIG_TARGET_DOMAIN, CONFIG_TARGET_PORT_NUMBER);
    ret = SSL_connect(ssl);
    if (ret <= 0) {
        ESP_LOGE(TAG, "failed, return [-0x%x]\n", -ret);
        goto failed7;
    }
    ESP_LOGI(TAG, "OK\n");

    ESP_LOGI(TAG, "send request to %s port %d ......", CONFIG_TARGET_DOMAIN, CONFIG_TARGET_PORT_NUMBER);
    ret = SSL_write(ssl, send_data, send_bytes);
    if (ret <= 0) {
        ESP_LOGE(TAG, "failed, return [-0x%x]\n", -ret);
        goto failed8;
    }
    ESP_LOGI(TAG, "OK\n");

    // 收发数据 
    do {
        ret = SSL_read(ssl, recv_buf, OPENSSL_CLIENT_RECV_BUF_LEN - 1);

        if (ret <= 0) {
            break;
        }

        recv_bytes += ret;
        recv_buf[ret] = '\0';
        ESP_LOGI(TAG, "%s", recv_buf);
    } while (1);

    ESP_LOGI(TAG, "read %d bytes data from %s ......", recv_bytes, CONFIG_TARGET_DOMAIN);

failed8:
    SSL_shutdown(ssl);
failed7:
    SSL_free(ssl);
failed6:
failed5:
failed4:
    close(sockfd);
failed3:
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

    xTaskCreate(&openssl_client_task, "openssl_client", 8192, NULL, 6, NULL);
}
