#include <string.h>
#include <sys/socket.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "tcpip_adapter.h"
#include "nvs_flash.h"
#include "dns_server.h"
#include "web_server.h"

static const char *TAG = "dns_server";

/*
*   流程：
*       第一步自己开启热点，这时候你就是相当于一个没外网的一个路由器啦！之后开启 dns 服务器，
*       再开启 tcp 服务器，等待客户端 dns 解析请求和 http 请求 ！
*/

#define EXAMPLE_ESP_WIFI_SSID      "ESP8266"        //WiFi名称
#define EXAMPLE_ESP_WIFI_PASS      "123456789"      //WiFi密码
#define EXAMPLE_MAX_STA_CONN       6                //WiFi最大连接个数

esp_err_t event_handler(void *ctx, system_event_t *event)
{
   switch(event->event_id) {

    /* AP模式下的其他设备连接上了本设备AP */
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR" join,AID=%d\n",
        MAC2STR(event->event_info.sta_connected.mac),
        event->event_info.sta_connected.aid);
        break;
        
    /* AP模式下,其他设备与本设备断开的连接 */
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "Wifi disconnected, try to connect ...");
        esp_wifi_connect();
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ESP8266设置为AP模式 */
void initilalise_wifi(void)
{
    tcpip_adapter_init();       // 初始化tcpip适配器
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );    // 建立一个事件循环
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();            // 获取一个默认的wifi配置参数结构体变量
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );                         // 根据cfg参数初始化wifi连接所需要的资源

    wifi_config_t ap_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,              /* WIFI名称 */
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),  /* WIFI名长度 */ 
            .password = EXAMPLE_ESP_WIFI_PASS,          /* 密码 */
            .max_connection = EXAMPLE_MAX_STA_CONN,     /* STA端最大连接个数 */ 
            .authmode = WIFI_AUTH_WPA_WPA2_PSK          /* 加密方式 WPA_WPA2_PSK */
        }
    };

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );             // 设置WiFi为AP模式
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP,&ap_config) );  // 设置ESP8266 AP的配置
    ESP_ERROR_CHECK( esp_wifi_start() );                            // 创建软ap控制块并启动软ap
    esp_wifi_connect();                                             // ESP8266 WiFi连接到AP
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());  //初始化默认的NVS分区 

    ESP_LOGI(TAG, "---------------------------------start the application---------------------------------");  
    ESP_LOGI(TAG, "SDK version:%s", esp_get_idf_version());

    initilalise_wifi();
    my_udp_init();
    xTaskCreate(&web_server2, "web_server2", 2048*2, NULL, 5, NULL);
}
