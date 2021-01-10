#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <esp_http_server.h>

static const char *TAG="APP";

/* Function to free context */
void adder_free_func(void *ctx)
{
    ESP_LOGI(TAG, "/adder Free Context function called");
    free(ctx);
}

/* 这个处理程序一直在累积发送给它的数据到per */
esp_err_t adder_post_handler(httpd_req_t *req)
{
    // 日志访客总量
    unsigned *visitors = (unsigned *)req->user_ctx;
    ESP_LOGI(TAG, "/adder visitor count = %d", ++(*visitors));

    char buf[10];
    char outbuf[50];
    int  ret;

    // 读取请求中接收到的数据
    ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) 
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) 
        {
            // 发送HTTP 408消息
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    int val = atoi(buf);
    ESP_LOGI(TAG, "/adder handler read %d", val);

    // 如果没有可用的会话上下文，则创建会话上下文
    if (! req->sess_ctx) 
    {
        ESP_LOGI(TAG, "/adder allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        req->free_ctx = adder_free_func;
        *(int *)req->sess_ctx = 0;
    }

    // 将接收到的数据添加到上下文
    int *adder = (int *)req->sess_ctx;
    *adder += val;

    snprintf(outbuf, sizeof(outbuf),"%d", *adder);
    httpd_resp_send(req, outbuf, strlen(outbuf));       // 发送完整的HTTP响应
    return ESP_OK;
}

/* 此处理程序获取累加器的当前值*/
esp_err_t adder_get_handler(httpd_req_t *req)
{
    // 日志访客总量
    unsigned *visitors = (unsigned *)req->user_ctx;
    ESP_LOGI(TAG, "/adder visitor count = %d", ++(*visitors));

    char outbuf[50];

    // 如果没有可用的会话上下文，则创建会话上下文
    if (! req->sess_ctx) 
    {
        ESP_LOGI(TAG, "/adder GET allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        req->free_ctx = adder_free_func;
        *(int *)req->sess_ctx = 0;
    }
    ESP_LOGI(TAG, "/adder GET handler send %d", *(int *)req->sess_ctx);

    snprintf(outbuf, sizeof(outbuf),"%d", *((int *)req->sess_ctx));
    httpd_resp_send(req, outbuf, strlen(outbuf));       // 发送完整的HTTP响应
    return ESP_OK;
}

/* 此处理程序将重置累加器的值 */
esp_err_t adder_put_handler(httpd_req_t *req)
{
    // 日志访客总量
    unsigned *visitors = (unsigned *)req->user_ctx;
    ESP_LOGI(TAG, "/adder visitor count = %d", ++(*visitors));

    char buf[10];
    char outbuf[50];
    int  ret;

    // 读取请求中接收到的数据
    ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) 
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) 
        {
            // 发送HTTP 408消息
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    int val = atoi(buf);
    ESP_LOGI(TAG, "/adder PUT handler read %d", val);

    // 如果没有可用的会话上下文，则创建会话上下文
    if (! req->sess_ctx) 
    {
        ESP_LOGI(TAG, "/adder PUT allocating new session");
        req->sess_ctx = malloc(sizeof(int));
        req->free_ctx = adder_free_func;
    }
    *(int *)req->sess_ctx = val;

    snprintf(outbuf, sizeof(outbuf),"%d", *((int *)req->sess_ctx));
    httpd_resp_send(req, outbuf, strlen(outbuf));       // 发送完整的HTTP响应
    return ESP_OK;
}

/* 维护一个存储次数的变量
 * the "/adder" URI has been visited */
static unsigned visitors = 0;

httpd_uri_t adder_post = {
    .uri      = "/adder",               // 要处理的URI
    .method   = HTTP_POST,              // URI支持的方法
    .handler  = adder_post_handler,     // 为支持的请求方法调用的处理程序。这必须返回ESP_OK，否则底层套接字将被关闭。
    .user_ctx = &visitors               // 指向用户上下文数据的指针
};

httpd_uri_t adder_get = {
    .uri      = "/adder",
    .method   = HTTP_GET,
    .handler  = adder_get_handler,
    .user_ctx = &visitors
};

httpd_uri_t adder_put = {
    .uri      = "/adder",
    .method   = HTTP_PUT,
    .handler  = adder_put_handler,
    .user_ctx = &visitors
};

/* 启动httpd服务器 */
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // TCP接收和发送HTTP流量的端口号
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    httpd_handle_t server;

    if (httpd_start(&server, &config) == ESP_OK) 
    {
        // 注册URI处理程序
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &adder_post);
        //httpd_register_uri_handler(server, &adder_get);
        //httpd_register_uri_handler(server, &adder_put);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // 停止httpd服务器
    httpd_stop(server);
}

static httpd_handle_t server = NULL;        // HTTP服务器实例句柄

/* 停止网络服务器 */
static void disconnect_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) 
    {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_webserver(*server);
        *server = NULL;
    }
}

/* 开始网络服务器 */
static void connect_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) 
    {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());                  // 初始化默认的NVS分区 
    ESP_ERROR_CHECK(esp_netif_init());                  // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环

    ESP_ERROR_CHECK(example_connect());                 // 配置Wi-Fi或以太网，连接，等待IP

    // 向系统事件循环注册事件处理程序 STA连接到AP并从连接的AP获得IP
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    // 向系统事件循环注册事件处理程序 STA断开与AP的连接
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));

    //server = start_webserver();
}
