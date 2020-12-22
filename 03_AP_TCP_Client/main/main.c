#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "rom/ets_sys.h"

#include "esp_event_loop.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/pwm.h"

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


#include "led.h"
#include "usart.h"
#include "key.h"
#include "dht11.h"
#include "delay.h"
#include "oled.h"
#include "i2c_master.h"
#include "intr.h"

/*
*  TCP设备端创建流程
*  1.创建套接字
*  2.连接服务器
*  3.收发数据
*  4.关闭套接字
**/

static const char *TAG = "ESP8266_Client";

static EventGroupHandle_t wifi_event_group;         //建立一个事件组
const int WIFI_CONNECTED_BIT = BIT0;

tcpip_adapter_ip_info_t     local_ip;               //ip适配器IPV4地址信息


#define EXAMPLE_ESP_WIFI_SSID      "ESP8266"        //WiFi名称
#define EXAMPLE_ESP_WIFI_PASS      "123456789"      //WiFi密码
#define PORT_ADDR                  "192.168.4.2"    //服务器IP地址
#define EXAMPLE_MAX_STA_CONN       6                //WiFi最大连接个数

esp_err_t event_handler(void *ctx, system_event_t *event);
void wifi_init_softap(void);

/* 任务 */
#define START_TASK_PRIO			5		//任务优先级
#define START_STK_SIZE 			2048  	//任务堆栈大小	
TaskHandle_t StartTask_Handler;			//任务句柄
void start_task(void *pvParameters);	//任务函数

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) 
    {
        /* ESP8266 WiFi连接到AP */
        case SYSTEM_EVENT_AP_START:
            esp_wifi_connect();
            ESP_LOGI(TAG, "ESP8266 Connection successful");
            break;

        /* station模式下已完成连接并且获取到了IP */
        case SYSTEM_EVENT_STA_GOT_IP: 
            //将其ip信息打印出来
            ESP_LOGI(TAG, "获取到IP:%s\n",ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        break;

        /* AP模式下的其他设备连接上了本设备AP */
        case SYSTEM_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "connect station:"MACSTR" join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
            break;

        /* AP模式下,其他设备与本设备断开的连接 */
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "break station:"MACSTR" leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
            break;

        /* station模式下 本设备从AP断开/AP踢了本设备 */
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED==%d",event->event_id);
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;

        default:
            ESP_LOGI(TAG, "default = %d",event->event_id);
            break;
    }
    return ESP_OK;
}

/* ESP8266设置为AP模式 */
void wifi_init_softap(void)
{ 
    esp_err_t res_ap_get;
    /* 创建一个FreeRTOS事件标志组 */
    wifi_event_group = xEventGroupCreate();

    /* 初始化tcpip适配器 */
    tcpip_adapter_init();
    /* 建立一个事件循环 */
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    /* 获取一个默认的wifi配置参数结构体变量 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* 根据cfg参数初始化wifi连接所需要的资源 */
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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
    /* 设置ESP8266 AP的配置 */
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    /* 创建软ap控制块并启动软ap */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);

    /* 获取接口的IP信息 */
    res_ap_get = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &local_ip);
    if(res_ap_get == ESP_OK){
        ESP_LOGI(TAG,"get is success\n");
    }
    ESP_LOGI(TAG, "old_self_ip:"IPSTR"",IP2STR(&local_ip.ip));
    ESP_LOGI(TAG, "old_self_netmask:"IPSTR"",IP2STR(&local_ip.netmask));
    ESP_LOGI(TAG, "old_self_gw:"IPSTR"\n",IP2STR(&local_ip.gw));

    /* 停止DHCP服务器 */
    //tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);

    /* 修改AP端IP信息 */
    //IP4_ADDR(&local_ip.ip, 192, 168, 5, 1);
    //IP4_ADDR(&local_ip.gw, 192, 168, 5, 1);
    //IP4_ADDR(&local_ip.netmask, 255, 255, 255, 0);

    //ESP_LOGI(TAG,"set ip:"IPSTR"",IP2STR(&local_ip.ip));
    //ESP_LOGI(TAG,"set:netmask"IPSTR"",IP2STR(&local_ip.netmask));
    //ESP_LOGI(TAG,"set gw:"IPSTR"\n",IP2STR(&local_ip.gw));

    /* 设置接口的IP信息 */
    //tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &local_ip);
    /* 启动DHCP服务器 */
    //tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP);

    
    OLED_ShowString(0,0,(uint8_t *)"ESP8266 = Client");
    OLED_ShowString(0,2,(uint8_t *)"IP:");
	OLED_ShowIP(24,2,(uint8_t *)(&local_ip.ip));
    OLED_ShowString(0,4,(uint8_t *)"Remote = Server");
    OLED_ShowString(0,6,(uint8_t *)"IP:");
}


void start_task(void *pvParameters)
{
    char *servInetAddr = PORT_ADDR;
    char rx_buffer[128];            //设置接收缓冲
    char addr_str[128];             //IP

    char sendline[64] = "hello world\n";
    char tx_buffer[128] = {"ESP8266_WIFI_Send OK"};

    while (1)
    {
        /* 1.创建套节字 */
        int socid = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if(socid == -1){
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "套接字创建成功");

        /* 2.连接服务器 */
        struct sockaddr_in ServerAddr;
        memset(&ServerAddr,0,sizeof(ServerAddr));
        ServerAddr.sin_family = AF_INET;          //设定为ipv4
        ServerAddr.sin_port = htons(8888);        //设定服务器端口号
        ServerAddr.sin_addr.s_addr = inet_addr(servInetAddr); //设定服务器IP
        ServerAddr.sin_len = sizeof(ServerAddr);
        int cet = connect(socid, (struct sockaddr *)&ServerAddr, ServerAddr.sin_len);
        if(cet == -1) {
            ESP_LOGE(TAG, "Socket connect failed!\n");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if (write(socid, sendline, strlen(sendline)) < 0) {
            ESP_LOGE(TAG, "Socket send failed\n");
            close(socid);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "连接服务器成功\n");

        while(1)
        {
            /* 发送数据 */
            int err = send(socid ,tx_buffer, strlen(tx_buffer), 0);
            if (err < 0) {
                ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
                break;
            }

            memset(&rx_buffer, 0, sizeof(rx_buffer));
            int len = recv(socid, rx_buffer, sizeof(rx_buffer), 0);
            if(len > 0)
            {               
                /* 获取发送方的ip地址作为字符串 */
                if (ServerAddr.sin_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&ServerAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str)-1);
                }
 
                /* null -终止接收到的任何内容，并像对待字符串一样对待它们 */
                rx_buffer[len] = '\0'; 
                /* 打印收到的数据长度以及服务器IP地址 */
                ESP_LOGI(TAG, "Received %d bytes from %s:%u ", len, addr_str, ntohs(ServerAddr.sin_port));
                OLED_ShowString(24,6,(uint8_t *)addr_str);
                /* 打印收到的数据 */
                ESP_LOGI(TAG, "%s", rx_buffer);
                if((strcmp(rx_buffer,"LED_ON") == 0) || (strcmp(rx_buffer,"led_on") == 0))
                {
                    LED_ON;
                }
                else if((strcmp(rx_buffer,"LED_OFF") == 0) || (strcmp(rx_buffer,"led_off") == 0))
                {
                    LED_OFF;
                }
            }
            else if(len == 0)
            {
                /* 服务端或本地段意外/手动断开 */
                close(socid);
                /* 跳出接收循环这样完成自动重连 */
                break;
            }
        }
        close(socid);
    }
    vTaskDelete(NULL);
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

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
	wifi_init_softap();					//设置ESP8266为AP模式

	xTaskCreate((TaskFunction_t) start_task,		//任务函数	
				(const char*   ) "start_task",		//任务名称
				(uint16_t      ) START_STK_SIZE,	//任务堆栈大小
				(void *        ) NULL,				//传递给任务函数的参数
				(UBaseType_t   ) START_TASK_PRIO,	//任务优先级
				(TaskHandle_t *) StartTask_Handler);//任务句柄  
}
