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
#include "led.h"

static const char *TAG = "PUBLISH_TEST";

static EventGroupHandle_t mqtt_event_group;

/* MQTT事件处理函数 */
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    static bool LED_STATE = 1;
    char mag_data[100];
    // 获取MQTT客户端结构体指针
    esp_mqtt_client_handle_t client = event->client;

    // 通过事件ID来分别处理对应的事件
    switch (event->event_id) 
    {
        // 建立连接成功
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_client cnnnect to EMQ ok. ");
            // 向主题"ESP8266_Publish"发生信息，消息为“I am ESP8266”,自动计算有效载荷，qos=1
            esp_mqtt_client_publish(client, "ESP8266_Publish", "I am ESP8266", 0, 1, 0);
            // 订阅主题，qos=0
            esp_mqtt_client_subscribe(client, "ESP8266_Subscribe", 0);
            break;

        // 客户端断开连接   
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_client have disconnected. ");
            break;

        // 主题订阅成功
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "mqtt subscribe ok. msg_id = %d ",event->msg_id);
            break;

        // 取消订阅
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "mqtt unsubscribe ok. msg_id = %d ",event->msg_id);
            break;

        // 主题发布成功
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "mqtt published ok. msg_id = %d ",event->msg_id);
            break;

        // 已收到订阅的主题消息
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "mqtt received topic: %.*s ",event->topic_len, event->topic);
            ESP_LOGI(TAG, "topic data: [%.*s] ", event->data_len, event->data);
            sprintf(mag_data,"%.*s",event->data_len, event->data);
            if(strcmp(mag_data,"LED_ON") == 0)
            {
                LED_ON;
                LED_STATE = 0;
            }      
            else if(strcmp(mag_data,"LED_OFF") == 0)
            {
                LED_OFF;
                LED_STATE = 1;
            }
            else if (strcmp(mag_data,"LED_IS") == 0)
            {
                if(LED_STATE == 0)
                    esp_mqtt_client_publish(client, "ESP8266_Publish", "LED_STATE_ON", 0, 1, 0);
                else
                    esp_mqtt_client_publish(client, "ESP8266_Publish", "LED_STATE_OFF", 0, 1, 0);
            }    
            break;

        // 客户端遇到错误
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR ");
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
    esp_mqtt_client_config_t mqtt_cfg = {
        .host = "192.168.0.107",                // MQTT服务端域名/IP地址
        .client_id = "ESP8266_MQTT",            // 客户端标识符
        .username = "admin",                    // MQTT用户名
        .password = "public",                   // MQTT密码
        .event_handle = mqtt_event_handler,     // MQTT事件处理函数
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size()); // 获取芯片可用内存
    // 获取一个MQTT客户端结构体指针，参数是MQTT客户端配置结构体
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    // 开启MQTT
    esp_mqtt_client_start(client);
}

void app_main()
{
    LED_Init();

    ESP_LOGI(TAG, "\n\n-----------------------Program starts-----------------------");

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size()); // 获取芯片可用内存
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());          // 返回完整的IDF版本字符串

    ESP_ERROR_CHECK(nvs_flash_init());                      // 初始化默认的NVS分区 
    ESP_ERROR_CHECK(esp_netif_init());                      // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());       // 创建默认事件循环

    ESP_ERROR_CHECK(example_connect());                     // 配置Wi-Fi或以太网，连接，等待IP

    mqtt_app_start();
}
