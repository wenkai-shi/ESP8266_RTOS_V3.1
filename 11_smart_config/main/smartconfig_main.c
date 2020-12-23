/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_smartconfig.h"
#include "smartconfig_ack.h"

#include "led.h"
#include "key.h"
#include "oled.h"
#include "dht11.h"

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char* TAG = "smartconfig";

static void smartconfig_example_task(void* parm);

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // 系统事件为WiFi事件 事件id为STA开始
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    } 

    // 系统事件为WiFi事件 事件id为失去STA连接
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } 

    // 系统事件为ip地址事件，且事件id为成功获取ip地址
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;     /* 获取IP地址信息*/
        OLED_ShowIP(24,2,(uint8_t *)(&event->ip_info.ip));
		OLED_ShowString(0,4,(uint8_t *)"Connect to WIFI ");
		OLED_ShowString(0,6,(uint8_t *)"Successfully    ");
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } 

    // 事件为smartconfig事件 smartconfig已经完成了APs扫描
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } 

    // 事件为smartconfig事件 smartconfig已经找到目标AP的频道
    else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } 

    // 事件为smartconfig事件 smartconfig获得SSID和密码
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t* evt = (smartconfig_event_got_ssid_pswd_t*)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;

        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_ERROR_CHECK(esp_wifi_disconnect());     // 从AP断开ESP8266 WiFi
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));    // 设置ESP8266 STA的配置
        ESP_ERROR_CHECK(esp_wifi_connect());        // 连接ESP8266 WiFi到AP
    } 
    
    // 事件为smartconfig事件 smartconfig已经发送ACK到手机
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();       // 初始化tcpip适配器
    s_wifi_event_group = xEventGroupCreate();           // 创建一个FreeRTOS事件标志组

    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();// 获取一个默认的wifi配置参数结构体变量
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));               // 根据cfg参数初始化wifi连接所需要的资源

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));     // 设置电流省电类型

    // 将事件处理程序注册到系统默认事件循环 分别是WiFi事件、IP地址、smartconfig事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));  // 设置WiFi为STA模式
    ESP_ERROR_CHECK(esp_wifi_start());                  // 开启wifi状态机
}

static void smartconfig_example_task(void* parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS));        // 设置SmartConfig的协议类型
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

    while (1) 
    {
        // 使用事件标志组等待连接建立(WIFI_CONNECTED_BIT)或连接失败(WIFI_FAIL_BIT)事件
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);

        if (uxBits & CONNECTED_BIT)     // 如果获取的标志位是成功获取IP，则表示成功连接到了Wi-Fi	
        {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }

        if (uxBits & ESPTOUCH_DONE_BIT) // 如果获取的标志位是一键配置的回调表示成功连接到了Wi-Fi，则停止配置
        {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();     // 停止SmartConfig，释放esp_smartconfig_start占用的缓冲区
            vTaskDelete(NULL);
        }
    }
}

void app_main()
{
    LED_Init();             //LED初始化
    KEY_Init();             //key
	OLED_Init();			//OLED
	DHT11_init();			//DHT11

	ESP_ERROR_CHECK(nvs_flash_init());  //初始化默认的NVS分区   

    ESP_LOGI(TAG, "---------------------------------start the application---------------------------------");  
    ESP_LOGI(TAG, "SDK version:%s", esp_get_idf_version());

	OLED_ShowString(0,0,(uint8_t *)"ESP8266 = STA");	
	OLED_ShowString(0,2,(uint8_t *)"IP:");				
	OLED_ShowString(0,4,(uint8_t *)"WIFI Connecting ");
	OLED_ShowString(0,6,(uint8_t *)"................");

    ESP_ERROR_CHECK(nvs_flash_init());
    initialise_wifi();
}

