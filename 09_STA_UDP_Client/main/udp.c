#include <string.h>
#include <sys/socket.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "udp.h"
#include "oled.h"
#include "led.h"

static const char *TAG = "udp";

EventGroupHandle_t       			udp_event_group;        //建立一个事件组

#define EXAMPLE_ESP_WIFI_SSID       "Tenda_5E"       //要连接的WiFi名称
#define EXAMPLE_ESP_WIFI_PASS       "SQ123456"       //连接WiFi的密码
#define PORT_ADDR                   "192.168.0.107"  //服务器IP地址
#define EXAMPLE_MAX_STA_CONN        10               //WiFi最大连接连接次数
#define SERVICE_PORT 				8888


static int mysocket;
static struct sockaddr_in remote_addr;
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
            xEventGroupSetBits(udp_event_group, UDP_CONNCETED_SUCCESS);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } 

    /* 系统事件为ip地址事件，且事件id为成功获取ip地址 */
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;     /* 获取IP地址信息*/
        ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));   /* 打印ip地址*/
        OLED_ShowString(0,0,(uint8_t *)"ESP8266 = Client");
        OLED_ShowString(0,2,(uint8_t *)"IP:");
        OLED_ShowIP(24,2,(uint8_t *)(&event->ip_info.ip));
        s_retry_num = 0;                                                /* WiFi重连次数清零 */

        /* 将WiFi连接FreeRTOS事件标志组的WiFi连接成功事件位置1 */
        xEventGroupSetBits(udp_event_group, WIFI_CONNECTED_BIT);
    }
}

//wifi的softap初始化
void wifi_init_softap(void) 
{
    /* 创建一个FreeRTOS事件标志组 */
	udp_event_group = xEventGroupCreate();

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

    /* 使用事件标志组等待连接建立(WIFI_CONNECTED_BIT)或连接失败(UDP_CONNCETED_SUCCESS)事件 */
    EventBits_t bits = xEventGroupWaitBits(udp_event_group,                    /* 需要等待的事件标志组的句柄 */
                                           WIFI_CONNECTED_BIT | UDP_CONNCETED_SUCCESS,  /* 需要等待的事件位 */
                                           pdFALSE,         /* 为pdFALSE时，在退出此函数之前所设置的这些事件位不变，为pdFALSE则清零*/ 
                                           pdFALSE,         /* 为pdFALSE时，设置的这些事件位任意一个置1就会返回，为pdFALSE则需全为1才返回 */
                                           portMAX_DELAY);  /* 设置为最长阻塞等待时间，单位为时钟节拍 */

    /* 根据事件标志组等待函数的返回值获取WiFi连接状态 */
    if (bits & WIFI_CONNECTED_BIT)  /* WiFi连接成功事件 */
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } 
    else if (bits & UDP_CONNCETED_SUCCESS)  /* WiFi连接失败事件 */
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } 
    else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");  /* 没有等待到事件 */
    }
}

/* 创建udp服务器套接字 */
esp_err_t create_udp_client(void)
{
    /* 1.创建套节字 */
	mysocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (mysocket < 0) 
	{
		show_socket_error_reason(mysocket);
		return ESP_FAIL;
	}

	remote_addr.sin_family = AF_INET;               //协议IPV4
	remote_addr.sin_port = htons(SERVICE_PORT);     //设定服务器端口号
	remote_addr.sin_addr.s_addr = inet_addr(PORT_ADDR);//设定服务器IP

	return ESP_OK;
}

void send_recv_data(void *pvParameters) 
{
	ESP_LOGI(TAG, "Send and receive data ");

	int send_len;
	char sendBuff[1024] = "hello, this is first message ...\n";

	send_len = sendto(mysocket, sendBuff, 1024, 0, (struct sockaddr *) &remote_addr, sizeof(remote_addr));
	if (send_len > 0) {
		ESP_LOGI(TAG, "succeed transfer data to %s:%u\n", inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port));
        OLED_ShowString(0,4,(uint8_t *)"Remote = Server");
    	OLED_ShowString(0,6,(uint8_t *)"IP:");
		OLED_ShowString(24,6,(uint8_t *)(inet_ntoa(remote_addr.sin_addr)));
		xEventGroupSetBits(udp_event_group, UDP_CONNCETED_SUCCESS);
	} 
	else {
		show_socket_error_reason(mysocket);
		close(mysocket);
		vTaskDelete(NULL);
	}
}

/* 获取套接字错误代码 */
int get_socket_error_code(int socket) 
{
	int result;
	u32_t optlen = sizeof(int);
	if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &result, &optlen) == -1) {
		ESP_LOGE(TAG, "getsockopt failed");
		return -1;
	}
	return result;
}

/* 显示套接字错误代码 */
int show_socket_error_reason(int socket) 
{
	int err = get_socket_error_code(socket);
	ESP_LOGW(TAG, "socket error %d %s", err, strerror(err));
	return err;
}

/* 检查连接的套接字 */
int check_connected_socket() 
{
	int ret;
	ESP_LOGD(TAG, "check connect_socket");
	ret = get_socket_error_code(mysocket);
	if (ret != 0) {
		ESP_LOGW(TAG, "socket error %d %s", ret, strerror(ret));
	}
	return ret;
}

/* 关闭套接字 */
void close_socket() 
{
	close(mysocket);
}
