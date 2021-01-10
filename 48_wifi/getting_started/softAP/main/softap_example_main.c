/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

static const char *TAG = "wifi softAP";

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // WiFi事件  AP模式下的其他设备连接上了本设备AP
    if (event_id == WIFI_EVENT_AP_STACONNECTED) 
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    }

    // WiFi事件  AP模式下其他设备与本设备断开的连接
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap()
{
    tcpip_adapter_init();               // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());       // 创建默认事件循环

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();    // 获取一个默认的wifi配置参数结构体变量
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                   // 根据cfg参数初始化wifi连接所需要的资源

    // 建立一个事件循环 WiFi事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    /* 设置参数结构体 */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,              /* WIFI名称 */
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),  /* WIFI名长度 */ 
            .password = EXAMPLE_ESP_WIFI_PASS,          /* 密码 */
            .max_connection = EXAMPLE_MAX_STA_CONN,     /* STA端最大连接个数 */ 
            .authmode = WIFI_AUTH_WPA_WPA2_PSK          /* 加密方式 WPA_WPA2_PSK */
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        /* 如果密码长度为0 则不设置加密 */
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    /* 设置WiFi为AP模式 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    /* 设置ESP8266 AP的配置信息 */
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    /* 创建soft-AP控制块并启动soft-AP */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());  //初始化默认的NVS分区

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
}
