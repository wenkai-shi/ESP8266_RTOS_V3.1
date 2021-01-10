/* SoftAP based Provisioning Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "esp_netif.h"
#include <lwip/err.h>
#include <lwip/sys.h>

#include "app_prov.h"

#define EXAMPLE_AP_RECONN_ATTEMPTS  CONFIG_EXAMPLE_AP_RECONN_ATTEMPTS

static const char *TAG = "app";

static void event_handler(void* arg, esp_event_base_t event_base, int event_id, void* event_data)
{
    static int s_retry_num = 0;

    // WiFi事件 STA开始
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "connection to AP successful");
    } 
    
    // WiFi事件 STA失去连接
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (s_retry_num < EXAMPLE_AP_RECONN_ATTEMPTS) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } 
    
    // ip地址事件 成功获取ip地址
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
    }
}

static void wifi_init_sta(void)
{
    // 将事件处理程序注册到系统默认事件循环，分别是WiFi事件和IP地址事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));

    // 以STA模式启动Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void start_softap_provisioning(void)
{
    int security = 0;           // 安全版本
    const protocomm_security_pop_t *pop = NULL; // 所有权证明

#ifdef CONFIG_EXAMPLE_USE_SEC_1
    security = 1;
#endif

    // 是否拥有财产证明  可选
#ifdef CONFIG_EXAMPLE_USE_POP
    const static protocomm_security_pop_t app_pop = {
        .data = (uint8_t *) CONFIG_EXAMPLE_POP,     // 指向包含拥有数据证明的缓冲区的指针
        .len = (sizeof(CONFIG_EXAMPLE_POP)-1)       // 拥有证明资料的长度(以字节为单位)
    };
    pop = &app_pop;
#endif

        const char *ssid = NULL;

#ifdef CONFIG_EXAMPLE_SSID
        ssid = CONFIG_EXAMPLE_SSID;
#else
        uint8_t eth_mac[6];
        // WiFi堆栈配置参数传递给esp_wifi_init调用
        esp_wifi_get_mac(WIFI_IF_STA, eth_mac);

        char ssid_with_mac[33];
        snprintf(ssid_with_mac, sizeof(ssid_with_mac), "PROV_%02X%02X%02X", eth_mac[3], eth_mac[4], eth_mac[5]);

        ssid = ssid_with_mac;
#endif

    ESP_ERROR_CHECK(app_prov_start_softap_provisioning(ssid, CONFIG_EXAMPLE_PASS, security, pop));
}

void app_main(void)
{
    ESP_LOGI(TAG, "\n\n-----------------------Program starts-----------------------");

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size()); // 获取芯片可用内存
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());          // 返回完整的IDF版本字符串


    ESP_ERROR_CHECK(nvs_flash_init());                      // 初始化默认的NVS分区 
    ESP_ERROR_CHECK(esp_netif_init());                      // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());       // 创建默认事件循环

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();    // 获取一个默认的wifi配置参数结构体变量
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));                   // 根据cfg参数初始化wifi连接所需要的资源

    bool provisioned;
    if (app_prov_is_provisioned(&provisioned) != ESP_OK)    // 检查设备是否准备就绪
    {
        ESP_LOGE(TAG, "Error getting device provisioning state");
        return;
    }

    if (provisioned == false)                               // 如果没有供应，通过softAP开始供应
    {
        ESP_LOGI(TAG, "Starting WiFi SoftAP provisioning");
        start_softap_provisioning();
    }
    else                                                    // 使用配置期间设置的凭据启动WiFi sta
    {
        ESP_LOGI(TAG, "Starting WiFi station");
        wifi_init_sta();
    }
}
