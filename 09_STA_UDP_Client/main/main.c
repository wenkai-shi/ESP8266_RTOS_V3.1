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
#include "udp.h"

/*
* UDP服务端创建流程：
*   1.创建数据报套接字 -- socket()
*   2.绑定端口  -- bind()
*   3.收发数据	-- sendto() recvfrom()
*   4.关闭套接字 -- closesocket()
*/

static const char *TAG = "ESP8266_Client";


static void udp_conn(void *pvParameters) 
{
	ESP_LOGI(TAG, "task udp_conn start... ");
	//等待是否已经成功连接到路由器的标志位
	xEventGroupWaitBits(udp_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

	/* 5s后连接server */
	vTaskDelay(5000 / portTICK_RATE_MS);

    //创建客户端并且检查是否创建成功
	ESP_LOGI(TAG, "Now Let us create udp client ... ");
	if (create_udp_client() == ESP_FAIL) {
		ESP_LOGI(TAG, "client create socket error ");
		vTaskDelete(NULL);
	} else {
		ESP_LOGI(TAG, "client create socket Succeed ");
	}

	//创建一个发送和接收数据的任务
	TaskHandle_t tx_rx_task;
	xTaskCreate(&send_recv_data, "send_recv_data", 4096, NULL, 4, &tx_rx_task);

	//等待 UDP连接成功标志位
	xEventGroupWaitBits(udp_event_group, UDP_CONNCETED_SUCCESS, false, true, portMAX_DELAY);

	vTaskDelete(tx_rx_task);
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

    xTaskCreate(&udp_conn, "udp_conn", 4096, NULL, 5, NULL);
}
