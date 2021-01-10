/* WiFi Connection Example using WPA2 Enterprise
 *
 * Original Copyright (C) 2006-2016, ARM Limited, All Rights Reserved, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD, Apache 2.0 License.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_event_loop.h"

/* The examples use simple WiFi configuration that you can set via
   'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"

   You can choose EAP method via 'make menuconfig' according to the
   configuration of AP.
*/
#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_EAP_METHOD CONFIG_EAP_METHOD

#define EXAMPLE_EAP_ID CONFIG_EAP_ID
#define EXAMPLE_EAP_USERNAME CONFIG_EAP_USERNAME
#define EXAMPLE_EAP_PASSWORD CONFIG_EAP_PASSWORD

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;

/* Constants that aren't configurable in menuconfig */
#define EAP_PEAP 1
#define EAP_TTLS 2

static const char* TAG = "example";

/* CA cert, taken from wpa2_ca.pem
   Client cert, taken from wpa2_client.crt
   Client key, taken from wpa2_client.key

   The PEM, CRT and KEY file were provided by the person or organization
   who configured the AP with wpa2 enterprise.

   To embed it in the app binary, the PEM, CRT and KEY file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern uint8_t ca_pem_start[] asm("_binary_wpa2_ca_pem_start");
extern uint8_t ca_pem_end[]   asm("_binary_wpa2_ca_pem_end");
extern uint8_t client_crt_start[] asm("_binary_wpa2_client_crt_start");
extern uint8_t client_crt_end[]   asm("_binary_wpa2_client_crt_end");
extern uint8_t client_key_start[] asm("_binary_wpa2_client_key_start");
extern uint8_t client_key_end[]   asm("_binary_wpa2_client_key_end");

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // 系统事件为WiFi事件 事件id为STA开始
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    
    // 系统事件为WiFi事件 事件id为失去STA连接
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        system_event_sta_disconnected_t *event = (system_event_sta_disconnected_t *)event_data;

        if (event->reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        }
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } 
    
    // 系统事件为ip地址事件，且事件id为成功获取ip地址
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void initialise_wifi(void)
{
    unsigned int ca_pem_bytes = ca_pem_end - ca_pem_start;
    unsigned int client_crt_bytes = client_crt_end - client_crt_start;
    unsigned int client_key_bytes = client_key_end - client_key_start;

    tcpip_adapter_init();                               // 初始化tcpip适配器
    wifi_event_group = xEventGroupCreate();             // 创建一个FreeRTOS事件标志组
    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();// 获取一个默认的wifi配置参数结构体变量
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));               // 根据cfg参数初始化wifi连接所需要的资源

    // 将事件处理程序注册到系统默认事件循环 分别是WiFi事件、IP地址
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));    // 设置WiFi API配置存储类型 全部存储到内存中
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,  // WiFi名
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));                              // 设置为STA模式
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));            // 设置STA配置信息
    // “PEAP/TTLS方法”配置“CA证书”
    ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_ca_cert(ca_pem_start, ca_pem_bytes)); 
    // 设置客户端证书和密钥
    ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_cert_key(client_crt_start, client_crt_bytes, client_key_start, client_key_bytes, NULL, 0));
    // 设置PEAP/TTLS方法的标识
    ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)EXAMPLE_EAP_ID, strlen(EXAMPLE_EAP_ID)));

    if (EXAMPLE_EAP_METHOD == EAP_PEAP || EXAMPLE_EAP_METHOD == EAP_TTLS) {
        // PEAP/TTLS方法设置“用户名”
        ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_username((uint8_t*)EXAMPLE_EAP_USERNAME, strlen(EXAMPLE_EAP_USERNAME)));
        // “PEAP/TTLS方法设置密码”
        ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_password((uint8_t*)EXAMPLE_EAP_PASSWORD, strlen(EXAMPLE_EAP_PASSWORD)));
    }

    ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_enable());    // 启用wpa2企业认证
    ESP_ERROR_CHECK(esp_wifi_start());                  // 启动WiFi
}

static void wpa2_enterprise_example_task(void* pvParameters)
{
    tcpip_adapter_ip_info_t ip;
    memset(&ip, 0, sizeof(tcpip_adapter_ip_info_t));
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    while (1) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);

        // 在适配器库中有一个IP信息副本，如果接口启动，则从接口获取IP信息，否则从副本获取IP信息
        if (tcpip_adapter_get_ip_info(ESP_IF_WIFI_STA, &ip) == 0) {
            ESP_LOGI(TAG, "~~~~~~~~~~~");
            ESP_LOGI(TAG, "IP:"IPSTR, IP2STR(&ip.ip));
            ESP_LOGI(TAG, "MASK:"IPSTR, IP2STR(&ip.netmask));
            ESP_LOGI(TAG, "GW:"IPSTR, IP2STR(&ip.gw));
            ESP_LOGI(TAG, "~~~~~~~~~~~");
        }

        ESP_LOGI(TAG, "free heap:%d\n", esp_get_free_heap_size());
    }
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());  // 初始化默认的NVS分区  
    initialise_wifi();                  // 初始化WiFi
    xTaskCreate(&wpa2_enterprise_example_task, "wpa2_enterprise_example_task", 4096, NULL, 5, NULL);
}
