#include <stdlib.h>
#include <stdbool.h>

#include <esp_log.h>
#include <esp_system.h>
#include <esp_http_server.h>

#include "tests.h"

static const char *TAG="TESTS";

int pre_start_mem, post_stop_mem, post_stop_min_mem;
bool basic_sanity = true;

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

/********************* 基本处理程序开始 *******************/

esp_err_t hello_get_handler(httpd_req_t *req)
{
#define STR "Hello World!"
    ESP_LOGI(TAG, "Free Stack for server task: '%u'", uxTaskGetStackHighWaterMark(NULL));
    httpd_resp_send(req, STR, strlen(STR));     // 发送完整的HTTP响应
    return ESP_OK;
#undef STR
}

esp_err_t hello_type_get_handler(httpd_req_t *req)
{
#define STR "Hello World!"
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);  // 设置响应的"内容类型"字段。默认的内容类型是'text/html'。
    httpd_resp_send(req, STR, strlen(STR));     // 发送完整的HTTP响应
    return ESP_OK;
#undef STR
}

esp_err_t hello_status_get_handler(httpd_req_t *req)
{
#define STR "Hello World!"
    httpd_resp_set_status(req, HTTPD_500);      // 将HTTP响应的状态设置为指定的值。默认情况下，'200 OK'响应作为响应发送。
    httpd_resp_send(req, STR, strlen(STR));     // 发送完整的HTTP响应
    return ESP_OK;
#undef STR
}

esp_err_t echo_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/echo handler read content length %d", req->content_len);

    char*  buf = malloc(req->content_len + 1);
    size_t off = 0;
    int    ret;

    if (!buf) 
    {
        // HTTP 500的Helper功能发送HTTP 500消息。如果希望在响应正文中发送额外的数据，直接使用低层函数。
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    while (off < req->content_len) 
    {
        // 读取请求中接收到的数据 (从HTTP请求中读取HTTP内容数据到提供的缓冲区中)
        ret = httpd_req_recv(req, buf + off, req->content_len - off);
        if (ret <= 0) 
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) 
            {
                // 发送HTTP 408消息
                httpd_resp_send_408(req);
            }
            free (buf);
            return ESP_FAIL;
        }
        off += ret;
        ESP_LOGI(TAG, "/echo handler recv length %d", ret);
    }
    buf[off] = '\0';

    if (req->content_len < 128) 
    {
        ESP_LOGI(TAG, "/echo handler read %s", buf);
    }

    // 搜索自定义报头字段
    char*  req_hdr = 0;
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Custom");
    if (hdr_len) 
    {
        // 读取自定义头值
        req_hdr = malloc(hdr_len + 1);
        if (req_hdr) 
        {
            // 从请求标头获取字段的值字符串
            httpd_req_get_hdr_value_str(req, "Custom", req_hdr, hdr_len + 1);

            // 设置为响应包的附加头
            httpd_resp_set_hdr(req, "Custom", req_hdr);
        }
    }
    httpd_resp_send(req, buf, req->content_len);        // 发送完整的HTTP响应
    free (req_hdr);
    free (buf);
    return ESP_OK;
}

void adder_free_func(void *ctx)
{
    ESP_LOGI(TAG, "Custom Free Context function called");
    free(ctx);
}

/* 创建一个上下文，在上下文中保持递增值
 * 收到。返回结果
 */
esp_err_t adder_post_handler(httpd_req_t *req)
{
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

esp_err_t leftover_data_post_handler(httpd_req_t *req)
{
    // 只回显请求的前10个字节，留下其余的按原样请求数据。 
    char buf[11];
    int  ret;

    // 读取请求中接收到的数据
    ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
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
    ESP_LOGI(TAG, "leftover data handler read %s", buf);
    httpd_resp_send(req, buf, strlen(buf));     // 发送完整的HTTP响应
    return ESP_OK;
}

int httpd_default_send(httpd_handle_t hd, int sockfd, const char *buf, unsigned buf_len, int flags);
void generate_async_resp(void *arg)
{
    char buf[250];
    struct async_resp_arg *resp_arg = (struct async_resp_arg *)arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
#define HTTPD_HDR_STR      "HTTP/1.1 200 OK\r\n"                   \
                           "Content-Type: text/html\r\n"           \
                           "Content-Length: %d\r\n"
#define STR "Hello Double World!"

    ESP_LOGI(TAG, "Executing queued work fd : %d", fd);

    snprintf(buf, sizeof(buf), HTTPD_HDR_STR, strlen(STR));
    httpd_default_send(hd, fd, buf, strlen(buf), 0);
    // 基于set_header发送附加消息头的空间
    httpd_default_send(hd, fd, "\r\n", strlen("\r\n"), 0);
    httpd_default_send(hd, fd, STR, strlen(STR), 0);
#undef STR
    free(arg);
}

esp_err_t async_get_handler(httpd_req_t *req)
{
#define STR "Hello World!"
    httpd_resp_send(req, STR, strlen(STR));     // 发送完整的HTTP响应
    // 注册一个HTTPD工作，它发送相同的数据再套接字
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    resp_arg->hd = req->handle;
    // 返回在接收HTTP请求时为其执行URI处理程序的会话的套接字描述符
    resp_arg->fd = httpd_req_to_sockfd(req);
    if (resp_arg->fd < 0) 
    {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Queuing work fd : %d", resp_arg->fd);
    // 异步执行一个工作函数排队
    httpd_queue_work(req->handle, generate_async_resp, resp_arg);
    return ESP_OK;
#undef STR
}


httpd_uri_t basic_handlers[] = {
    { .uri      = "/hello/type_html",       // 要处理的URI
      .method   = HTTP_GET,                 // URI支持的方法
      .handler  = hello_type_get_handler,   // 为支持的请求方法调用的处理程序。这必须返回ESP_OK，否则底层套接字将被关闭。
      .user_ctx = NULL,                     // 指向用户上下文数据的指针，handler可以使用这些数据
    },
    { .uri      = "/hello",
      .method   = HTTP_GET,
      .handler  = hello_get_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/hello/status_500",
      .method   = HTTP_GET,
      .handler  = hello_status_get_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/echo",
      .method   = HTTP_POST,
      .handler  = echo_post_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/echo",
      .method   = HTTP_PUT,
      .handler  = echo_post_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/leftover_data",
      .method   = HTTP_POST,
      .handler  = leftover_data_post_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/adder",
      .method   = HTTP_POST,
      .handler  = adder_post_handler,
      .user_ctx = NULL,
    },
    { .uri      = "/async_data",
      .method   = HTTP_GET,
      .handler  = async_get_handler,
      .user_ctx = NULL,
    }
};

int basic_handlers_no = sizeof(basic_handlers)/sizeof(httpd_uri_t);

/* 注册基本处理程序 */
void register_basic_handlers(httpd_handle_t hd)
{
    int i;
    ESP_LOGI(TAG, "Registering basic handlers");
    ESP_LOGI(TAG, "No of handlers = %d", basic_handlers_no);
    for (i = 0; i < basic_handlers_no; i++) 
    {
        // 注册URI处理程序
        if (httpd_register_uri_handler(hd, &basic_handlers[i]) != ESP_OK) 
        {
            ESP_LOGW(TAG, "register uri failed for %d", i);
            return;
        }
    }
    ESP_LOGI(TAG, "Success");
}

httpd_handle_t test_httpd_start()
{
    // 获取可用堆的大小 注意:返回值可能大于可以分配的最大连续块
    pre_start_mem = esp_get_free_heap_size();
    httpd_handle_t hd;          // HTTP服务器实例句柄  服务器的每个实例都有一个唯一的句柄
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); // HTTP服务器配置结构
    config.server_port = 1234;  // TCP接收和发送HTTP流量的端口号

    // 任何时候连接的套接字/客户端的最大数量
    config.max_open_sockets = (CONFIG_LWIP_MAX_SOCKETS - 3);

    if (httpd_start(&hd, &config) == ESP_OK) 
    {
        ESP_LOGI(TAG, "Started HTTP server on port: '%d'", config.server_port); // 启动HTTP服务器端口
        ESP_LOGI(TAG, "Max URI handlers: '%d'", config.max_uri_handlers);       // 最大URI处理程序
        ESP_LOGI(TAG, "Max Open Sessions: '%d'", config.max_open_sockets);      // 客户端的最大数量
        ESP_LOGI(TAG, "Max Header Length: '%d'", HTTPD_MAX_REQ_HDR_LEN);        // 最大支持的HTTP请求头长度
        ESP_LOGI(TAG, "Max URI Length: '%d'", HTTPD_MAX_URI_LEN);               // 最大支持的HTTP请求URI长度
        ESP_LOGI(TAG, "Max Stack Size: '%d'", config.stack_size);               // 服务器任务允许的最大堆栈大小
        return hd;
    }
    return NULL;
}

void test_httpd_stop(httpd_handle_t hd)
{
    httpd_stop(hd);
    post_stop_mem = esp_get_free_heap_size();   // 获取可用堆的大小 注意:返回值可能大于可以分配的最大连续块
    ESP_LOGI(TAG, "HTTPD Stop: Current free memory: %d", post_stop_mem);
}

httpd_handle_t start_tests()
{
    // 开始网络服务器
    httpd_handle_t hd = test_httpd_start();
    if (hd) {
        register_basic_handlers(hd);
    }
    return hd;
}

void stop_tests(httpd_handle_t hd)
{
    ESP_LOGI(TAG, "Stopping httpd");
    // 停止网络服务器
    test_httpd_stop(hd);
}
