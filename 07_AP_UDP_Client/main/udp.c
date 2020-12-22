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

//服务器的地址：这里的 255.255.255.255是在局域网发送，不指定某个设备
#define SERVER_IP 					"255.255.255.255"
#define EXAMPLE_ESP_WIFI_SSID      	"ESP8266"        		//WiFi名称
#define EXAMPLE_ESP_WIFI_PASS      	"123456789"      		//WiFi密码
#define PORT_ADDR                  	"192.168.4.2"    		//服务器IP地址
#define EXAMPLE_MAX_STA_CONN       	6                		//WiFi最大连接个数
#define SERVICE_PORT 				8888

tcpip_adapter_ip_info_t     local_ip;               		//ip适配器IPV4地址信息

static int mysocket;

static struct sockaddr_in remote_addr;

static esp_err_t event_handler(void *ctx, system_event_t *event) 
{
	switch (event->event_id) 
    {
        /* ESP8266 WiFi连接到AP */
	    case SYSTEM_EVENT_STA_START:
		    esp_wifi_connect();
			ESP_LOGI(TAG, "ESP8266 Connection successful");
		    break;

        /* station模式下 本设备从AP断开/AP踢了本设备 */
	    case SYSTEM_EVENT_STA_DISCONNECTED:
		    esp_wifi_connect();
		    xEventGroupClearBits(udp_event_group, WIFI_CONNECTED_BIT);
		    break;

        case SYSTEM_EVENT_STA_CONNECTED:
            break;

        /* station模式下已完成连接并且获取到了IP */
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "event_handler:SYSTEM_EVENT_STA_GOT_IP!");
            ESP_LOGI(TAG, "got ip:%s\n", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            xEventGroupSetBits(udp_event_group, WIFI_CONNECTED_BIT);
            break;

        /* AP模式下的其他设备连接上了本设备AP */
        case SYSTEM_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "station:" MACSTR " join,AID=%d",
                     MAC2STR(event->event_info.sta_connected.mac),
                     event->event_info.sta_connected.aid);
            xEventGroupSetBits(udp_event_group, WIFI_CONNECTED_BIT);
            break;

        /* AP模式下,其他设备与本设备断开的连接 */
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "station:" MACSTR "leave,AID=%d",
                     MAC2STR(event->event_info.sta_disconnected.mac),
                     event->event_info.sta_disconnected.aid);
            xEventGroupSetBits(udp_event_group, UDP_CONNCETED_SUCCESS);
            xEventGroupClearBits(udp_event_group, WIFI_CONNECTED_BIT);
            break;

        default:
            break;
    }
	return ESP_OK;
}

//wifi的softap初始化
void wifi_init_softap(void) 
{
	esp_err_t res_ap_get;
    /* 创建一个FreeRTOS事件标志组 */
	udp_event_group = xEventGroupCreate();

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

	ESP_LOGI(TAG, " Wifi_init_softap finished the SSID: %s password:%s ", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);

    /* 获取接口的IP信息 */
    res_ap_get = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &local_ip);
    if(res_ap_get == ESP_OK){
        ESP_LOGI(TAG,"get is success");
    }
    ESP_LOGI(TAG, "old_self_ip:"IPSTR"",IP2STR(&local_ip.ip));
    ESP_LOGI(TAG, "old_self_netmask:"IPSTR"",IP2STR(&local_ip.netmask));
    ESP_LOGI(TAG, "old_self_gw:"IPSTR"\n",IP2STR(&local_ip.gw));

    OLED_ShowString(0,0,(uint8_t *)"ESP8266 = Client");
    OLED_ShowString(0,2,(uint8_t *)"IP:");
	OLED_ShowIP(24,2,(uint8_t *)(&local_ip.ip));
    OLED_ShowString(0,4,(uint8_t *)"Remote = Server");
    OLED_ShowString(0,6,(uint8_t *)"IP:");
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
    ESP_LOGI(TAG, "socket Creating successful");

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
