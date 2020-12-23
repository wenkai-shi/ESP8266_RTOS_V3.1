#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"

#include "lwip/opt.h"


static const char *TAG = "key_trigger";

//宏定义一个GPIO
#define KEY_GPIO    0

xQueueHandle gpio_evt_queue = NULL;      //队列

typedef enum {
	KEY_SHORT_PRESS = 1, 
    KEY_LONG_PRESS,
} alink_key_t;

/* 中断服务函数 */
static void gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);     //传递数据到队列中
}

void KeyInit(uint32_t key_gpio_pin)
{
    gpio_config_t gpio_config_structure;                            // 定义一个gpio配置结构体

    // 初始化gpio配置结构体
    gpio_config_structure.mode = GPIO_MODE_INPUT;                   // 输出模式
    gpio_config_structure.pin_bit_mask = (1ULL << key_gpio_pin);    // 引脚
    gpio_config_structure.pull_down_en = 0;                         // 禁止下拉
    gpio_config_structure.pull_up_en = 0;                           // 禁止上拉
    gpio_config_structure.intr_type = GPIO_INTR_ANYEDGE;            // 上升/下降沿触发中断
    gpio_config(&gpio_config_structure);                            // 根据设定参数初始化并使能

	gpio_install_isr_service(1);                                    // 注册中断服务
	gpio_isr_handler_add(key_gpio_pin, gpio_isr_handler, (void*) key_gpio_pin);    // 设置GPIO的中断回调函数

    gpio_set_intr_type(key_gpio_pin, GPIO_INTR_ANYEDGE);            // 设置上升/下降沿触发中断
	gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));            // 创建一个消息队列，从中获取队列句柄
}

esp_err_t alink_key_scan(TickType_t ticks_to_wait) 
{
	uint32_t io_num;
	BaseType_t press_key = pdFALSE;
	BaseType_t lift_key = pdFALSE;
    static int count = 0;

	while (1) {

		//接收从消息队列发来的消息
		xQueueReceive(gpio_evt_queue, &io_num, ticks_to_wait);

		// 按键按下
		while (gpio_get_level(io_num) == 0) {
			press_key = pdTRUE;
            count++;
            vTaskDelay(50 / portTICK_RATE_MS);
		} 
        
		// 按键抬起
        if (gpio_get_level(io_num)) {
			lift_key = pdTRUE;
		}

		//近当按下标志位和按键弹起标志位都为1时候，才执行回调
		if (press_key & lift_key) {
			press_key = pdFALSE;
			lift_key = pdFALSE;
            ESP_LOGI(TAG, "count = %d",count);
			//如果大于2s则回调长按，否则就短按回调
			if (count > 40) {
                count = 0;
				return KEY_LONG_PRESS;
			} 
            else {
                count = 0;
				return KEY_SHORT_PRESS;
			}
		}
	}
}

void key_trigger(void *arg)
{
	esp_err_t ret = 0;
	KeyInit(KEY_GPIO);

	while (1) {
		ret = alink_key_scan(portMAX_DELAY);
		//如果错误则跳出任务
		if (ret == -1)
			vTaskDelete(NULL);

		switch (ret) {
		case KEY_SHORT_PRESS:
			ESP_LOGI(TAG, "短按触发回调 ... \r\n");
			break;

		case KEY_LONG_PRESS:
			ESP_LOGI(TAG, "长按触发回调 ... \r\n");
			break;

		default:
			break;
		}
	}

}

void app_main()
{
    xTaskCreate(key_trigger, "key_trigger", 1024 * 2, NULL, 10,NULL);
}


