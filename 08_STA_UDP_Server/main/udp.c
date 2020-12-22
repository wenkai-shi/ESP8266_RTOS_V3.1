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

#define EXAMPLE_ESP_WIFI_SSID      "Tenda_5E"        		//WiFi名称
#define EXAMPLE_ESP_WIFI_PASS      "SQ123456"      			//WiFi密码
#define EXAMPLE_MAX_STA_CONN       6                		//WiFi最大连接个数
#define SERVICE_PORT 8266

static int mysocket;

static struct sockaddr_in remote_addr;
static unsigned int socklen;

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
			OLED_ShowString(0,0,(uint8_t *)"ESP8266 = Server");
    		OLED_ShowString(0,2,(uint8_t *)"IP:");
			OLED_ShowString(24,2,(uint8_t *)(ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip)));
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

//wifi的sta初始化
void wifi_init_sta(void) 
{
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

	ESP_LOGI(TAG, "wifi_init_sta finished ");
	ESP_LOGI(TAG, "connect to ap SSID:%s password:%s ",EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}


/* 创建udp服务器套接字 */
esp_err_t create_udp_server(void)
{
    /* 1.创建套节字 */
	mysocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (mysocket < 0) 
	{
		show_socket_error_reason(mysocket);
		return ESP_FAIL;
	}

	struct sockaddr_in server_addr;                 //存储server地址结构
	server_addr.sin_family = AF_INET;               //协议IPV4
	server_addr.sin_port = htons(SERVICE_PORT);     //端口，转换为网络字节序(short int)
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);//本地地址，并转换为网络字节序(long int)

    /* 2.绑定 将IP和端口号与套节字绑定 */
	if (bind(mysocket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) 
	{
		show_socket_error_reason(mysocket);
		close(mysocket);
		return ESP_FAIL;
	}
	return ESP_OK;
}

void send_recv_data(void *pvParameters) 
{
	ESP_LOGI(TAG, "Send and receive data ");

	int len;
	int result;
	char rx_buff[1024];
	char tx_buff[1024] = {"ESP8266_WIFI_Send OK"};

	socklen = sizeof(remote_addr);
	memset(rx_buff, 0, sizeof(rx_buff));

	while (1) 
	{
		//每次接收都要清空接收数组
		memset(rx_buff, 0x00, sizeof(rx_buff));
		//开始接收
		len = recvfrom(mysocket, rx_buff, sizeof(rx_buff), 0,(struct sockaddr *) &remote_addr, &socklen);
		if(len < 0)
		{
			if (LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG) 
			{
				show_socket_error_reason(mysocket);
			}
		}

		/* 收到数据 */
		else
		{
			/* null -终止接收到的任何内容，并像对待字符串一样对待它们 */
			rx_buff[len] = '\0';
			/* 打印收到的数据长度以及设备端IP地址和端口号 */
			ESP_LOGI(TAG, "Received %d bytes from %s:%u ", len, inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port));
			//打印接收到的数组ntohs(from.sin_port)
			ESP_LOGI(TAG, "recvData: %s ", rx_buff);		
			OLED_ShowString(0,4,(uint8_t *)"Remote = Client");
    		OLED_ShowString(0,6,(uint8_t *)"IP:");
			OLED_ShowString(24,6,(uint8_t *)(inet_ntoa(remote_addr.sin_addr)));

			if ((strcmp(rx_buff, "LED_ON") == 0) || (strcmp(rx_buff, "led_on") == 0)){
				LED_ON;
			}
			else if ((strcmp(rx_buff, "LED_OFF") == 0) || (strcmp(rx_buff, "led_off") == 0)){
				LED_OFF;
			}

			/* 向客户端发送数据 */
			result = sendto(mysocket, tx_buff, strlen(tx_buff), 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
			if (result < 0){
				ESP_LOGE(TAG, "sendto failed ");
			}
			else{
				ESP_LOGI(TAG, "sendto Succeed \n");
			}
		}
	}
	close_socket();
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
