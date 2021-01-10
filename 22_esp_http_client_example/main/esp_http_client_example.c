#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_tls.h"

#include "esp_http_client.h"

#define MAX_HTTP_RECV_BUFFER        512
#define MAX_HTTP_OUTPUT_BUFFER      2048
static const char *TAG = "HTTP_CLIENT";

/* Root cert for howsmyssl.com, taken from howsmyssl_com_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect www.howsmyssl.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const char howsmyssl_com_root_cert_pem_start[] asm("_binary_howsmyssl_com_root_cert_pem_start");
extern const char howsmyssl_com_root_cert_pem_end[]   asm("_binary_howsmyssl_com_root_cert_pem_end");

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // 用于存储来自事件处理程序的http请求响应的缓冲区
    static int output_len;       // Stores读取的字节数
    switch(evt->event_id) 
    {
        // 当执行过程中出现任何错误时，将发生此事件
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;

        // 一但HTTP连接到服务器，就不会进行数据交换
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;

        // 在接收从服务器发送的每个报头时发生
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;

        // 当从服务器接收数据时发生，可能是数据包的多个部分
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  在本例中使用的分块编码的URL返回二进制数据时，会添加分块编码检查。然而，在使用分块编码的情况下也可以使用事件处理程序。
             */
            // 检查响应数据是分块的
            if (!esp_http_client_is_chunked_response(evt->client)) 
            {
                // 如果配置了user_data buffer，将响应复制到缓冲区中
                if (evt->user_data) 
                {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } 
                else 
                {
                    if (output_buffer == NULL) 
                    {
                        // 获取http响应内容长度
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) 
                        {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }
            break;

        // 在完成HTTP会话时发生    
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) 
            {
                // 响应在 output_buffer 中累积。取消下面一行的注释以打印累积的响应
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            break;

        // 已断开连接
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) 
            {
                if (output_buffer != NULL) 
                {
                    free(output_buffer);
                    output_buffer = NULL;
                    output_len = 0;
                }
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}

static void http_rest_with_url(void)
{
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
    /**
     * NOTE: All the configuration parameters for http_client must be spefied either in URL or as host and path parameters.
     * If host and path parameters are not set, query parameter will be ignored. In such cases,
     * query parameter should be specified in URL.
     *
     * If URL as well as host and path parameters are specified, values of host and path will be considered.
     */
    esp_http_client_config_t config = {
        .host = "httpbin.org",                      // 域名或者IP
        .path = "/get",                             // HTTP路径
        .query = "esp",                             // HTTP查询
        .event_handler = _http_event_handler,       // HTTP事件处理
        .user_data = local_response_buffer,         // 传递本地缓冲区地址以获得响应
    };
    // 初始化http client
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET 请求
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),            // 获取http响应状态码
                esp_http_client_get_content_length(client));        // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    // 在Info级别记录一个十六进制字节的缓冲区
    ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, strlen(local_response_buffer));

    // POST 请求
    const char *post_data = "{\"field1\":\"value1\"}";
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "http://httpbin.org/post");
    // 设置http请求方式 POST
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    // 设置http请求头
    esp_http_client_set_header(client, "Content-Type", "application/json");
    // 设置post数据
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // PUT请求
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "http://httpbin.org/put");
    // 设置http请求方式 PUT
    esp_http_client_set_method(client, HTTP_METHOD_PUT);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP PUT Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "HTTP PUT request failed: %s", esp_err_to_name(err));
    }

    // PATCH请求
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "http://httpbin.org/patch");
    // 设置http请求方式 PATCH
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    // 设置post数据
    esp_http_client_set_post_field(client, NULL, 0);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP PATCH Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "HTTP PATCH request failed: %s", esp_err_to_name(err));
    }

    // DELETE请求
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "http://httpbin.org/delete");
    // 设置http请求方式 DELETE
    esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP DELETE Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "HTTP DELETE request failed: %s", esp_err_to_name(err));
    }

    // HEAD请求
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "http://httpbin.org/get");
    // 设置http请求方式 DELETE
    esp_http_client_set_method(client, HTTP_METHOD_HEAD);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP HEAD Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "HTTP HEAD request failed: %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void http_rest_with_hostname_path(void)
{
    esp_http_client_config_t config = {
        .host = "httpbin.org",                      // 域名或者IP
        .path = "/get",                             // HTTP路径
        .transport_type = HTTP_TRANSPORT_OVER_TCP,  // HTTP传输类型
        .event_handler = _http_event_handler,       // HTTP事件处理
    };
    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET 请求
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    // POST 请求
    const char *post_data = "field1=value1&field2=value2";
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "/post");
    // 设置http请求方式 POST
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    // 设置post数据
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // PUT 请求
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "/put");
    // 设置http请求方式 PUT
    esp_http_client_set_method(client, HTTP_METHOD_PUT);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP PUT Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } else {
        ESP_LOGE(TAG, "HTTP PUT request failed: %s", esp_err_to_name(err));
    }

    // PATCH 请求
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "/patch");
    // 设置http请求方式 PATCH
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    // 设置post数据
    esp_http_client_set_post_field(client, NULL, 0);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP PATCH Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } else {
        ESP_LOGE(TAG, "HTTP PATCH request failed: %s", esp_err_to_name(err));
    }

    // DELETE 请求
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "/delete");
    // 设置http请求方式 DELETE
    esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP DELETE Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } else {
        ESP_LOGE(TAG, "HTTP DELETE request failed: %s", esp_err_to_name(err));
    }

    // HEAD 请求
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "/get");
    // 设置http请求方式 HEAD
    esp_http_client_set_method(client, HTTP_METHOD_HEAD);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP HEAD Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } else {
        ESP_LOGE(TAG, "HTTP HEAD request failed: %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void http_auth_basic(void)
{
    esp_http_client_config_t config = {
        .url = "http://user:passwd@httpbin.org/basic-auth/user/passwd",     // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .event_handler = _http_event_handler,                               // HTTP事件处理
        .auth_type = HTTP_AUTH_TYPE_BASIC,                                  // Http认证类型
    };

    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP Basic Auth Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void http_auth_basic_redirect(void)
{
    esp_http_client_config_t config = {
        .url = "http://user:passwd@httpbin.org/basic-auth/user/passwd",     // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .event_handler = _http_event_handler,                               // HTTP事件处理
    };

    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP Basic Auth redirect Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void http_auth_digest(void)
{
    esp_http_client_config_t config = {
        .url = "http://user:passwd@httpbin.org/digest-auth/auth/user/passwd/MD5/never", // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .event_handler = _http_event_handler,                                           // HTTP事件处理
    };

    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP Digest Auth Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void https_with_url(void)
{
    esp_http_client_config_t config = {
        .url = "https://www.howsmyssl.com",             // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .event_handler = _http_event_handler,           // HTTP事件处理
        .cert_pem = howsmyssl_com_root_cert_pem_start,  // 指向PEM或DER格式的证书数据，用于服务器验证(使用SSL)，默认为NULL，不需要验证服务器。
    };

    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void https_with_hostname_path(void)
{
    esp_http_client_config_t config = {
        .host = "www.howsmyssl.com",                    // 域名或者IP
        .path = "/",                                    // HTTP路径
        .transport_type = HTTP_TRANSPORT_OVER_SSL,      // HTTP传输类型
        .event_handler = _http_event_handler,           // HTTP事件处理
        .cert_pem = howsmyssl_com_root_cert_pem_start,  // 指向PEM或DER格式的证书数据，用于服务器验证(使用SSL)，默认为NULL，不需要验证服务器。
    };
    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void http_relative_redirect(void)
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/relative-redirect/3",    // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .event_handler = _http_event_handler,               // HTTP事件处理
    };
    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP Relative path redirect Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void http_absolute_redirect(void)
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/absolute-redirect/3",    // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .event_handler = _http_event_handler,               // HTTP事件处理
    };
    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTP Absolute path redirect Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void http_redirect_to_https(void)
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/redirect-to?url=https%3A%2F%2Fwww.howsmyssl.com",    // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .event_handler = _http_event_handler,                                           // HTTP事件处理
    };
    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP redirect to HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}


static void http_download_chunk(void)
{
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/stream-bytes/8912",  // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .event_handler = _http_event_handler,           // HTTP事件处理
    };
    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP chunk encoding Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void http_perform_as_stream_reader(void)
{
    char *buffer = malloc(MAX_HTTP_RECV_BUFFER + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Cannot malloc http receive buffer");
        return;
    }
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/get",        // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
    };

    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    // 打开连接，写入所有头字符串并返回
    if ((err = esp_http_client_open(client, 0)) != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        free(buffer);
        return;
    }
    // 从http流中读取，处理所有接收头
    int content_length =  esp_http_client_fetch_headers(client);
    int total_read_len = 0, read_len;
    if (total_read_len < content_length && content_length <= MAX_HTTP_RECV_BUFFER) 
    {
        // 从http流读取数据
        read_len = esp_http_client_read(client, buffer, content_length);
        if (read_len <= 0) 
        {
            ESP_LOGE(TAG, "Error read data");
        }
        buffer[read_len] = 0;
        ESP_LOGD(TAG, "read_len = %d", read_len);
    }
    ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),        // 获取http响应状态码
                    esp_http_client_get_content_length(client));    // 获取http响应内容长度

    // 关闭http连接，仍然保留所有http请求资源                
    esp_http_client_close(client);
    // 关闭 http client
    esp_http_client_cleanup(client);
    free(buffer);
}

static void https_async(void)
{
    esp_http_client_config_t config = {
        .url = "https://postman-echo.com/post",     // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .event_handler = _http_event_handler,       // HTTP事件处理
        .is_async = true,                           // 设置异步模式，目前只支持HTTPS
        .timeout_ms = 5000,                         // 网络超时(毫秒)
    };

    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err;
    const char *post_data = "Using a Palantír requires a person with great strength of will and wisdom. The Palantíri were meant to "
                            "be used by the Dúnedain to communicate throughout the Realms in Exile. During the War of the Ring, "
                            "the Palantíri were used by many individuals. Sauron used the Ithil-stone to take advantage of the users "
                            "of the other two stones, the Orthanc-stone and Anor-stone, but was also susceptible to deception himself.";

    // 设置http请求方式 POST                        
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    // 设置post数据
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    while (1) 
    {
        // 进行所有的选项调用，并按照选项中描述的方式执行传输
        err = esp_http_client_perform(client);
        if (err != ESP_ERR_HTTP_EAGAIN) 
        {
            break;
        }
    }
    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void https_with_invalid_url(void)
{
    esp_http_client_config_t config = {
            .url = "https://not.existent.url",      // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
            .event_handler = _http_event_handler,   // HTTP事件处理
    };

    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    // 进行所有的选项调用，并按照选项中描述的方式执行传输
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) 
    {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %d",
                 esp_http_client_get_status_code(client),       // 获取http响应状态码
                 esp_http_client_get_content_length(client));   // 获取http响应内容长度
    } 
    else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    // 关闭 http client
    esp_http_client_cleanup(client);
}

/*
 *  http_native_request() demonstrates use of low level APIs to connect to a server,
 *  make a http request and read response. Event handler is not used in this case.
 *  Note: This approach should only be used in case use of low level APIs is required.
 *  The easiest way is to use esp_http_perform()
 */
static void http_native_request(void)
{
    char output_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // 用于存储http请求响应的缓冲区
    int content_length = 0;
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/get",        // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
    };

    // 初始化 http client
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // 设置http请求方式 GET
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    // 打开连接，写入所有头字符串并返回
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } 
    else 
    {
        // 从http流中读取，处理所有接收头
        content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) 
        {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        }
        else 
        {
            // 从http流中读取响应
            int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) 
            {
                ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),        // 获取http响应状态码
                esp_http_client_get_content_length(client));    // 获取http响应内容长度
                ESP_LOG_BUFFER_HEX(TAG, output_buffer, strlen(output_buffer));
            } 
            else 
            {
                ESP_LOGE(TAG, "Failed to read response");
            }
        }
    }
    // 关闭http连接，仍然保留所有http请求资源
    esp_http_client_close(client);

    // POST Request
    const char *post_data = "{\"field1\":\"value1\"}";
    // 为客户端设置URL，当执行此行为时，URL中的选项将替换旧的选项
    esp_http_client_set_url(client, "http://httpbin.org/post");
    // 设置http请求方式 POST
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    // 设置http请求头
    esp_http_client_set_header(client, "Content-Type", "application/json");
    // 打开连接，写入所有头字符串并返回
    err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } 
    else 
    {
        // 把数据写入先前由esp_http_client_open()打开的HTTP连接
        int wlen = esp_http_client_write(client, post_data, strlen(post_data));
        if (wlen < 0) 
        {
            ESP_LOGE(TAG, "Write failed");
        }
        // 从http流中读取响应
        int data_read = esp_http_client_read_response(client, output_buffer, MAX_HTTP_OUTPUT_BUFFER);
        if (data_read >= 0) 
        {
            ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
            esp_http_client_get_status_code(client),        // 获取http响应状态码
            esp_http_client_get_content_length(client));    // 获取http响应内容长度
            ESP_LOG_BUFFER_HEX(TAG, output_buffer, strlen(output_buffer));
        } 
        else {
            ESP_LOGE(TAG, "Failed to read response");
        }
    }
    // 关闭 http client
    esp_http_client_cleanup(client);
}

static void http_test_task(void *pvParameters)
{
    //http_rest_with_url();
    //http_rest_with_hostname_path();

    // 配置ESP HTTP客户端启用基本认证
    //http_auth_basic();
    /*http_auth_basic_redirect();

    http_auth_digest();
    http_relative_redirect();
    http_absolute_redirect();
    https_with_url();
    https_with_hostname_path();
    http_redirect_to_https();
    http_download_chunk();
    http_perform_as_stream_reader();
    https_async();
    https_with_invalid_url();*/

    /*演示了如何使用低级api连接到服务器，发出http请求并读取响应。在这种情况下不使用事件处理程序。
    注意:这种方法应该只在需要使用低级api的情况下使用。最简单的方法是使用esp_http_perform()*/
    //http_native_request();

    ESP_LOGI(TAG, "Finish http example");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());                  // 初始化默认的NVS分区   
    ESP_ERROR_CHECK(esp_netif_init());                  // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环

    ESP_ERROR_CHECK(example_connect());                 // 配置Wi-Fi或以太网，连接，等待IP
    ESP_LOGI(TAG, "Connected to AP, begin http example");

    xTaskCreate(&http_test_task, "http_test_task", 2048*8, NULL, 5, NULL);
}
