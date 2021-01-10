/* Console example — WiFi commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "tcpip_adapter.h"
#include "esp_event_loop.h"
#include "cmd_wifi.h"

#include "esp_log.h"

static const char *TAG = "cmd_wifi";

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;

    switch(event->event_id) 
    {
        // station模式下已完成连接并且获取到了IP
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            ESP_LOGI(TAG, "Access to the IP:%s",ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            break;

        // station模式下 本设备从AP断开/AP踢了本设备
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI(__func__, "Disconnect reason : %d", info->disconnected.reason);
            if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT)
            {
                // 设置指定接口的协议类型默认协议为(WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G)
                esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
            }
            // 连接ESP8266 WiFi到AP
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;

        default:
            break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static bool initialized = false;
    if (initialized) {
        return;
    }
    
    tcpip_adapter_init();                                           // 初始化tcpip适配器
    wifi_event_group = xEventGroupCreate();                         // 创建一个FreeRTOS事件标志组
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );    // 建立一个事件循环
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();            // 获取一个默认的wifi配置参数结构体变量
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );                         // 根据cfg参数初始化wifi连接所需要的资源
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );      // 设置WiFi API配置存储类型 所有配置将只存储在内存中
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );           // 设置WiFi模式
    ESP_ERROR_CHECK( esp_wifi_start() );                            // 启动
    initialized = true;
}

static bool wifi_join(const char* ssid, const char* pass, int timeout_ms)
{
    // 初始化WiFi
    initialise_wifi();
    wifi_config_t wifi_config = { 0 };
    strncpy((char*) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strncpy((char*) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );            // 设置WiFi为STA模式
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );  // 设置ESP8266 STA的配置信息
    ESP_ERROR_CHECK( esp_wifi_connect() );                          // 启动

    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 1, 1, timeout_ms / portTICK_PERIOD_MS);
    return (bits & CONNECTED_BIT) != 0;
}

/** Arguments used by 'join' function */
static struct {
    struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} join_args;

static int connect(int argc, char** argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &join_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, join_args.end, argv[0]);
        return 1;
    }
    ESP_LOGI(__func__, "Connecting to '%s'", join_args.ssid->sval[0]);

    bool connected = wifi_join(join_args.ssid->sval[0], join_args.password->sval[0], join_args.timeout->ival[0]);
    if (!connected) {
        ESP_LOGW(__func__, "Connection timed out");
        return 1;
    }
    ESP_LOGI(__func__, "Connected");
    return 0;
}

void register_wifi()
{
    join_args.timeout = arg_int0(NULL, "timeout", "<t>", "Connection timeout, ms");
    join_args.timeout->ival[0] = 10000; // 设置默认值
    join_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    join_args.password = arg_str0(NULL, NULL, "<pass>", "PSK of AP");
    join_args.end = arg_end(2);

    const esp_console_cmd_t join_cmd = {
        .command = "join",                  // 命令名称
        .help = "Join WiFi AP as a station",// 命令的帮助文本
        .hint = NULL,                       // 提示文本，通常列出可能的参数
        .func = &connect,                   // 执行命令的函数
        .argtable = &join_args              // 指向arg_xxx结构体的指针的数组或结构
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&join_cmd) );
}
