#include <string.h>
#include <sys/socket.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "protocol_examples_common.h"

#include "coap.h"

const static char *TAG = "CoAP_server";

/* Set this to 9 to get verbose logging from within libcoap */
#define COAP_LOGGING_LEVEL 0

static char espressif_data[100];
static int espressif_data_len = 0;

// 资源处理程序 GET
static void hnd_espressif_get(coap_context_t *ctx, coap_resource_t *resource, coap_session_t *session, coap_pdu_t *request, 
                              coap_binary_t *token,  coap_string_t *query, coap_pdu_t *response)
{
    // 将适当的数据部分添加到响应pdu
    coap_add_data_blocked_response(resource, session, request, response, token,
                                   COAP_MEDIATYPE_TEXT_PLAIN, 0,
                                   (size_t)espressif_data_len,
                                   (const u_char *)espressif_data);
}

// 资源处理程序 PUT
static void hnd_espressif_put(coap_context_t *ctx,  coap_resource_t *resource,  coap_session_t *session,
                              coap_pdu_t *request, coap_binary_t *token, coap_string_t *query, coap_pdu_t *response)
{
    size_t size;
    unsigned char *data;

    // 为资源的所有观察者发送一个观察报文，如果不为空，可以选择匹配查询
    coap_resource_notify_observers(resource, NULL);

    if (strcmp (espressif_data, "no data") == 0) 
    {
        response->code = COAP_RESPONSE_CODE(201);
    }
    else 
    {
        response->code = COAP_RESPONSE_CODE(204);
    }

    // 获取指定PDU的长度和数据指针
    coap_get_data(request, &size, &data);

    if (size == 0) 
    {      
        /* re-init */
        snprintf(espressif_data, sizeof(espressif_data), "no data");
        espressif_data_len = strlen(espressif_data);
    } 
    else 
    {
        espressif_data_len = size > sizeof (espressif_data) ? sizeof (espressif_data) : size;
        memcpy (espressif_data, data, espressif_data_len);
    }
}

// 资源处理程序 DELETE
static void hnd_espressif_delete(coap_context_t *ctx,  coap_resource_t *resource,  coap_session_t *session,
                                 coap_pdu_t *request,  coap_binary_t *token,  coap_string_t *query,  coap_pdu_t *response)
{
    // 为资源的所有观察者发送一个观察报文，如果不为空，可以选择匹配查询
    coap_resource_notify_observers(resource, NULL);
    snprintf(espressif_data, sizeof(espressif_data), "no data");
    espressif_data_len = strlen(espressif_data);
    response->code = COAP_RESPONSE_CODE(202);
}

static void coap_example_thread(void *p)
{
    coap_context_t *ctx = NULL;                 // CoAP堆栈的全局状态
    coap_address_t   serv_addr;                 // 多功能地址结构体 sockaddr、sockaddr_in、sockaddr_in6
    coap_resource_t *resource = NULL;

    snprintf(espressif_data, sizeof(espressif_data), "no data");
    espressif_data_len = strlen(espressif_data);
    coap_set_log_level(COAP_LOGGING_LEVEL);     // 设置日志级别为指定的值
    while (1) 
    {
        coap_endpoint_t *ep_udp = NULL;
        coap_endpoint_t *ep_tcp = NULL;
        unsigned wait_ms;

        // 准备CoAP服务器socket
        coap_address_init(&serv_addr);
        serv_addr.addr.sin.sin_family      = AF_INET;                   // 协议IPV4
        serv_addr.addr.sin.sin_addr.s_addr = INADDR_ANY;                // 本地地址
        serv_addr.addr.sin.sin_port        = htons(COAP_DEFAULT_PORT);  // 端口，转换为网络字节序(short int)

        // 创建一个新的coap_context_t对象，它将保存CoAP堆栈状态
        ctx = coap_new_context(NULL);
        if (!ctx) {
            ESP_LOGE(TAG, "set up coap_new_context failed");
            continue;
        }
        ESP_LOGI(TAG, "set up coap_new_context succeed");

        // 创建一个UDP
        ep_udp = coap_new_endpoint(ctx, &serv_addr, COAP_PROTO_UDP);
        if (!ep_udp) {
            ESP_LOGE(TAG, "udp: coap_new_endpoint failed");
            goto clean_up;
        }
        ESP_LOGI(TAG, "udp: coap_new_endpoint succeed");

        // 创建一个TCP
        ep_tcp = coap_new_endpoint(ctx, &serv_addr, COAP_PROTO_TCP);
        if (!ep_tcp) {
            ESP_LOGE(TAG, "tcp: coap_new_endpoint failed");
            goto clean_up;
        }
        ESP_LOGI(TAG, "tcp: coap_new_endpoint succeed");

        // 创建一个新的资源对象并初始化 stringuri_path 的链接字段
        resource = coap_resource_init(coap_make_str_const("Espressif"), 0);
        if (!resource) {
            ESP_LOGE(TAG, "set up coap_resource_init failed");
           goto clean_up;
        }
        ESP_LOGI(TAG, "set up coap_resource_init succeed");

        // 将指定的处理程序注册为消息处理程序 请求类型为 GET PUT DELETE
        coap_register_handler(resource, COAP_REQUEST_GET, hnd_espressif_get);
        coap_register_handler(resource, COAP_REQUEST_PUT, hnd_espressif_put);
        coap_register_handler(resource, COAP_REQUEST_DELETE, hnd_espressif_delete);

        // 设置get资源可观察
        coap_resource_set_get_observable(resource, 1);
        // 为上下文注册给定的资源
        coap_add_resource(ctx, resource);

        wait_ms = COAP_RESOURCE_CHECK_TIME * 1000;

        while (1) 
        {
            int result = coap_run_once(ctx, wait_ms);
            if (result < 0) {
                break;
            } 
            else if (result && (unsigned)result < wait_ms) {
                // 如果返回结果等待时间，则递减
                wait_ms -= result;
            }
            if (result) {
                /* result must have been >= wait_ms, so reset wait_ms */
                wait_ms = COAP_RESOURCE_CHECK_TIME * 1000;
            }
        }
    }
clean_up:
    coap_free_context(ctx);     // 释放CoAP堆栈上下文
    coap_cleanup();

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());                  // 初始化默认的NVS分区   
    ESP_ERROR_CHECK(esp_netif_init());                  // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环

    ESP_ERROR_CHECK(example_connect());                 // 配置Wi-Fi或以太网，连接，等待IP

    xTaskCreate(coap_example_thread, "coap", 1024 * 5, NULL, 5, NULL);
}
