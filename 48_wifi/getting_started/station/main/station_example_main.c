/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

static EventGroupHandle_t s_wifi_event_group;       // FreeRTOS事件组在我们连接时发出信号

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // 系统事件为WiFi事件 事件id为STA开始
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)  
    {
        esp_wifi_connect();
    } 

    // 系统事件为WiFi事件 事件id为失去STA连接
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)        // WiFi重连次数小于EXAMPLE_ESP_MAXIMUM_RETRY
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } 
        else {
            // 将WiFi连接事件标志组的WiFi连接失败事件位置1
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } 

    // 系统事件为ip地址事件，且事件id为成功获取ip地址
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;     // 获取IP地址信息
        ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
        s_retry_num = 0;
        // 将WiFi连接FreeRTOS事件标志组的WiFi连接成功事件位置1
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();           // 创建一个FreeRTOS事件标志组

    tcpip_adapter_init();           // 初始化tcpip适配器

    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();// 获取一个默认的wifi配置参数结构体变量
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));               // 根据cfg参数初始化wifi连接所需要的资源

    // 将事件处理程序注册到系统默认事件循环，分别是WiFi事件和IP地址事件 
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // 设置参数结构体
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,              // WIFI名称
            .password = EXAMPLE_ESP_WIFI_PASS           // 密码
        },
    };

    // 如果接入点不支持WPA2，这些模式可以通过下面行启用
    if (strlen((char *)wifi_config.sta.password)) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );                     // 设置WiFi为STA模式
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );   // 将其wifi信息配置进去
    ESP_ERROR_CHECK(esp_wifi_start() );                                     // 开启wifi状态机

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* 使用事件标志组等待连接建立(WIFI_CONNECTED_BIT)或连接失败(WIFI_FAIL_BIT)事件 */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,                    /* 需要等待的事件标志组的句柄 */
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,  /* 需要等待的事件位 */
                                           pdFALSE,         /* 为pdFALSE时，在退出此函数之前所设置的这些事件位不变，为pdFALSE则清零*/ 
                                           pdFALSE,         /* 为pdFALSE时，设置的这些事件位任意一个置1就会返回，为pdFALSE则需全为1才返回 */
                                           portMAX_DELAY);  /* 设置为最长阻塞等待时间，单位为时钟节拍 */

    /* 根据事件标志组等待函数的返回值获取WiFi连接状态 */
    if (bits & WIFI_CONNECTED_BIT)  /* WiFi连接成功事件 */
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } 
    else if (bits & WIFI_FAIL_BIT)  /* WiFi连接失败事件 */
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } 
    else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");  /* 没有等待到事件 */
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
    vEventGroupDelete(s_wifi_event_group);
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();        // 设置ESP8266为sta模式
}
