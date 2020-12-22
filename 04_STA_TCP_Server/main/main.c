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
*  TCP服务端创建流程
*  1.创建套接字
*  2.绑定(IP、端口)
*  3.监听
*  4.socket接受客户端连接
*  5.收发数据
*  6.关闭套接字
**/

static const char *TAG = "ESP8266_Server";

static EventGroupHandle_t wifi_event_group;         //FreeRTOS事件组在连接时发出信号

tcpip_adapter_ip_info_t     local_ip;               //ip适配器IPV4地址信息

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0                     //通过IP连接到AP
#define WIFI_FAIL_BIT      BIT1                     //在最大重试次数后，未能连接

#define EXAMPLE_ESP_WIFI_SSID      "Tenda_5E"       //要连接的WiFi名称
#define EXAMPLE_ESP_WIFI_PASS      "SQ123456"       //连接WiFi的密码
#define EXAMPLE_MAX_STA_CONN       10               //WiFi最大连接连接次数

void wifi_init_sta(void);

/* 任务 */
#define START_TASK_PRIO			5		//任务优先级
#define START_STK_SIZE 			2048  	//任务堆栈大小	
TaskHandle_t StartTask_Handler;			//任务句柄
void start_task(void *pvParameters);	//任务函数

static int s_retry_num = 0;             // 记录wifi重连次数

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    /* 系统事件为WiFi事件 事件id为STA开始 */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
    }  

    /* 系统事件为WiFi事件 事件id为失去STA连接 */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (s_retry_num < EXAMPLE_MAX_STA_CONN)           /* WiFi重连次数小于10 */
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP %d times. \n",s_retry_num);
        }
        else 
        {
            /* 将WiFi连接事件标志组的WiFi连接失败事件位置1 */
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } 

    /* 系统事件为ip地址事件，且事件id为成功获取ip地址 */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;     /* 获取IP地址信息*/
        ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));   /* 打印ip地址*/
        OLED_ShowString(0,0,(uint8_t *)"ESP8266 = Server");
        OLED_ShowString(0,2,(uint8_t *)"IP:");
        OLED_ShowIP(24,2,(uint8_t *)(&event->ip_info.ip));
        s_retry_num = 0;                                                /* WiFi重连次数清零 */

        /* 将WiFi连接FreeRTOS事件标志组的WiFi连接成功事件位置1 */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ESP8266设置为AP模式 */
void wifi_init_sta(void)
{ 
    /* 创建一个FreeRTOS事件标志组 */
    wifi_event_group = xEventGroupCreate();

    /* 初始化tcpip适配器 */
    tcpip_adapter_init();
    /* 创建默认事件循环 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 获取一个默认的wifi配置参数结构体变量 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    /* 根据cfg参数初始化wifi连接所需要的资源 */
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* 设置参数结构体 */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,              /* WIFI名称 */
            .password = EXAMPLE_ESP_WIFI_PASS,          /* 密码 */
        },
    };

    /* 如果接入点不支持WPA2，这些模式可以通过下面行启用 */
    if (strlen((char *)wifi_config.sta.password)) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    /* 设置WiFi为STA模式 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    /* 将其wifi信息配置进去 */
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    /* 开启wifi状态机 */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* 使用事件标志组等待连接建立(WIFI_CONNECTED_BIT)或连接失败(WIFI_FAIL_BIT)事件 */
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,                    /* 需要等待的事件标志组的句柄 */
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
    vEventGroupDelete(wifi_event_group);
}


void start_task(void *pvParameters)
{
    char rx_buffer[128];    //设置接收缓冲
    char addr_str[128];     //IP
    int addr_family;
    int ip_protocol;

    char tx_buffer[128] = {"ESP8266_WIFI_Send OK"};   
    while (1)
    {
        struct sockaddr_in addr;                    //存储server地址结构
        addr.sin_addr.s_addr = htonl(INADDR_ANY);   //本地地址，并转换为网络字节序(long int)
        addr.sin_family = AF_INET;                  //协议IPV4
        addr.sin_port = htons(8266);                //端口，转换为网络字节序(short int)
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        /* 将网络地址转换成“.”点隔的字符串格式 */
        inet_ntoa_r(addr.sin_addr, addr_str, sizeof(addr_str) - 1);

        /* 1.创建套节字 */
        int socid = socket(addr_family, SOCK_STREAM, ip_protocol);
        if(socid == -1){
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "套接字创建成功");

        /* 2.绑定 将IP和端口号与套节字绑定 */
        int err = bind(socid, (struct sockaddr *)&addr, sizeof(addr));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG,"绑定成功");

        /* 3.监听 同时连接服务器的客户端个数 */
        err = listen(socid, 6);
        if(err == -1){
            ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG,"监听成功");

        /* 4.等待客户端连接 */
        struct sockaddr_in cliaddr;
        uint addrLen = sizeof(cliaddr);
        int accid = accept(socid, (struct sockaddr *)&cliaddr, &addrLen);
        if(accid == -1){
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG,"连接客户端成功");
    
        ESP_LOGI(TAG,"Connect client IP And Port：%s:%u",inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
        OLED_ShowString(0,4,(uint8_t *)"Remote = Client");
        OLED_ShowString(0,6,(uint8_t *)"IP:");
        OLED_ShowIP(24,6,(uint8_t *)(&cliaddr.sin_addr));

        while(1)
        {
            int len = recv(accid, rx_buffer, sizeof(rx_buffer) - 1, 0);//接收数据
            /* 接收过程中发生错误 */
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            /* 连接关闭了 */
            else if (len == 0) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }
            /* 收到数据 */
            else {
                /* 获取发送方的ip地址作为字符串 */
                if (cliaddr.sin_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&cliaddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                }
 
                /* null -终止接收到的任何内容，并像对待字符串一样对待它们 */
                rx_buffer[len] = '\0'; 
                /* 打印收到的数据长度以及设备端IP地址 */
                ESP_LOGI(TAG, "Received %d bytes from %s:%u ", len, addr_str,ntohs(cliaddr.sin_port));
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

                int err = send(accid, tx_buffer, strlen(tx_buffer), 0);//发送数据
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
                    break;
                }
            }
        }
        if (accid != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(accid, 0);
            close(accid);   //关闭accept
            close(socid);
        }
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

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	wifi_init_sta();					//设置ESP8266为sta模式

	xTaskCreate((TaskFunction_t) start_task,		//任务函数	
				(const char*   ) "start_task",		//任务名称
				(uint16_t      ) START_STK_SIZE,	//任务堆栈大小
				(void *        ) NULL,				//传递给任务函数的参数
				(UBaseType_t   ) START_TASK_PRIO,	//任务优先级
				(TaskHandle_t *) StartTask_Handler);//任务句柄  
}