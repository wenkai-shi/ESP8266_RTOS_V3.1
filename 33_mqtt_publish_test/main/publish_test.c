/* MQTT publish test

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "PUBLISH_TEST";

static EventGroupHandle_t mqtt_event_group;
const static int CONNECTED_BIT = BIT0;

static esp_mqtt_client_handle_t mqtt_client = NULL;

static char *expected_data = NULL;
static char *actual_data = NULL;
static size_t expected_size = 0;
static size_t expected_published = 0;
static size_t actual_published = 0;
static int qos_test = 0;

#if CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_eclipse_org_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_eclipse_org_pem_start[]   asm("_binary_mqtt_eclipse_org_pem_start");
#endif
extern const uint8_t mqtt_eclipse_org_pem_end[]   asm("_binary_mqtt_eclipse_org_pem_end");

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    static int msg_id = 0;
    static int actual_len = 0;
    // your_context_t *context = event->context;
    switch (event->event_id) 
    {
        // 建立连接成功
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            xEventGroupSetBits(mqtt_event_group, CONNECTED_BIT);
            // 向主题"ESP8266_Publish"发生信息，消息为“I am ESP32.”,自动计算有效载荷，qos=1
            esp_mqtt_client_publish(client, "ESP8266_Publish", "I am ESP8266.", 0, 1, 0);
            // 订阅主题，qos=0
            msg_id = esp_mqtt_client_subscribe(client, CONFIG_EXAMPLE_SUBSCIBE_TOPIC, qos_test);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            break;

        // 客户端断开连接
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        // 主题订阅成功
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        // 取消订阅
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        // 主题发布成功
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        // 数据事件 已收到订阅的主题消息
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            printf("ID=%d, total_len=%d, data_len=%d, current_data_offset=%d\n", event->msg_id, event->total_data_len, event->data_len, event->current_data_offset);
            if (event->topic)
            {
                actual_len = event->data_len;
                msg_id = event->msg_id;
            }
            else
            {
                actual_len += event->data_len;
                // check consisency with msg_id across multiple data events for single msg
                if (msg_id != event->msg_id)
                {
                    ESP_LOGI(TAG, "Wrong msg_id in chunked message %d != %d", msg_id, event->msg_id);
                    abort();
                }
            }
            memcpy(actual_data + event->current_data_offset, event->data, event->data_len);
            if (actual_len == event->total_data_len)
            {
                if (0 == memcmp(actual_data, expected_data, expected_size))
                {
                    printf("OK!");
                    memset(actual_data, 0, expected_size);
                    actual_published++;
                    if (actual_published == expected_published)
                    {
                        printf("Correct pattern received exactly x times\n");
                        ESP_LOGI(TAG, "Test finished correctly!");
                    }
                }
                else
                {
                    printf("FAILED!");
                    abort();
                }
            }
            break;

        // 发生错误事件
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}


static void mqtt_app_start(void)
{
    // 创建一个FreeRTOS事件标志组
    mqtt_event_group = xEventGroupCreate();
    // MQTT客户端配置结构
    const esp_mqtt_client_config_t mqtt_cfg = {
        .event_handle = mqtt_event_handler,                     // 注册MQTT事件句柄
        .cert_pem = (const char *)mqtt_eclipse_org_pem_start,   // 指向PEM或DER格式的证书数据，用于服务器验证(使用SSL)，默认为NULL，不需要验证服务器
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size()); // 获取芯片可用内存
    // 基于配置创建mqtt客户端句柄
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
}

static void get_string(char *line, size_t size)
{

    int count = 0;
    while (count < size) 
    {
        int c = fgetc(stdin);
        if (c == '\n') 
        {
            line[count] = '\0';
            break;
        } 
        else if (c > 0 && c < 127) 
        {
            line[count] = c;
            ++count;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void app_main()
{
    char line[256];
    char pattern[32];
    char transport[32];
    int repeat = 0;

    ESP_LOGI(TAG, "\n\n-----------------------Program starts-----------------------");

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size()); // 获取芯片可用内存
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());          // 返回完整的IDF版本字符串

    esp_log_level_set("*", ESP_LOG_INFO);                   // 描述正常事件流的信息消息
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);      // 更大的调试信息块或频繁的消息，这些消息可能会淹没输出
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());                      // 初始化默认的NVS分区 
    ESP_ERROR_CHECK(esp_netif_init());                      // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());       // 创建默认事件循环

    ESP_ERROR_CHECK(example_connect());                     // 配置Wi-Fi或以太网，连接，等待IP

    mqtt_app_start();

    while (1) 
    {
        // 获取字符串
        get_string(line, sizeof(line));
        sscanf(line, "%s %s %d %d %d", transport, pattern, &repeat, &expected_published, &qos_test);
        ESP_LOGI(TAG, "PATTERN:%s REPEATED:%d PUBLISHED:%d\n", pattern, repeat, expected_published);
        int pattern_size = strlen(pattern);
        free(expected_data);
        free(actual_data);
        actual_published = 0;
        expected_size = pattern_size * repeat;
        expected_data = malloc(expected_size);
        actual_data = malloc(expected_size);
        for (int i = 0; i < repeat; i++)
        {
            memcpy(expected_data + i * pattern_size, pattern, pattern_size);
        }
        printf("EXPECTED STRING %.*s, SIZE:%d\n", expected_size, expected_data, expected_size);
        // 停止mqtt客户机任务
        esp_mqtt_client_stop(mqtt_client);

        if (0 == strcmp(transport, "tcp")) {
            ESP_LOGI(TAG, "[TCP transport] Startup..");
            // 设置mqtt连接 TCP URI
            esp_mqtt_client_set_uri(mqtt_client, CONFIG_EXAMPLE_BROKER_TCP_URI);
        } 
        else if (0 == strcmp(transport, "ssl")) {
            ESP_LOGI(TAG, "[SSL transport] Startup..");
            // 设置mqtt连接 SSL URI
            esp_mqtt_client_set_uri(mqtt_client, CONFIG_EXAMPLE_BROKER_SSL_URI);
        } 
        else if (0 == strcmp(transport, "ws")) {
            ESP_LOGI(TAG, "[WS transport] Startup..");
            // 设置mqtt连接 WS URI
            esp_mqtt_client_set_uri(mqtt_client, CONFIG_EXAMPLE_BROKER_WS_URI);
        } 
        else if (0 == strcmp(transport, "wss")) {
            ESP_LOGI(TAG, "[WSS transport] Startup..");
            // 设置mqtt连接 WSS URI
            esp_mqtt_client_set_uri(mqtt_client, CONFIG_EXAMPLE_BROKER_WSS_URI);
        } 
        else {
            ESP_LOGE(TAG, "Unexpected transport");
            abort();
        }
        xEventGroupClearBits(mqtt_event_group, CONNECTED_BIT);
        // 使用已经创建的客户机句柄启动mqtt客户机
        esp_mqtt_client_start(mqtt_client);
        ESP_LOGI(TAG, "Note free memory: %d bytes", esp_get_free_heap_size());  // 获取芯片可用内存
        xEventGroupWaitBits(mqtt_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

        for (int i = 0; i < expected_published; i++) 
        {
            // MQTT客户端配置结构
            int msg_id = esp_mqtt_client_publish(mqtt_client, CONFIG_EXAMPLE_PUBLISH_TOPIC, expected_data, expected_size, qos_test, 0);
            ESP_LOGI(TAG, "[%d] Publishing...", msg_id);
        }
    }
}
