#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"

static const char *TAG = "gpio_interrupt";

//宏定义
#define GPIO_OUT_PIN 4

//设置GPIO高低电平输出
void fun_set_gpio_level(void)
{
    gpio_config_t gpio_config_structure;                        // 定义一个gpio配置结构体

    // 初始化gpio配置结构体
    gpio_config_structure.mode = GPIO_MODE_OUTPUT;              // 输出模式
    gpio_config_structure.pin_bit_mask = (1ULL << GPIO_OUT_PIN);// 引脚
    gpio_config_structure.pull_down_en = 0;                     // 禁止下拉
    gpio_config_structure.pull_up_en = 0;                       // 禁止上拉
    gpio_config_structure.intr_type = GPIO_INTR_DISABLE;        // 禁止中断

    gpio_config(&gpio_config_structure);                        // 根据设定参数初始化并使能

	gpio_set_level(GPIO_OUT_PIN, 1);                            // 输出高电平
}

//宏定义
#define GPIO_IN_PIN 0

//设置获取指定的GPIO的输入电平
void fun_get_gpio_level(void)
{
    gpio_config_t gpio_config_structure;                        // 定义一个gpio配置结构体

    // 初始化gpio配置结构体
    gpio_config_structure.mode = GPIO_MODE_INPUT;               // 输出模式
    gpio_config_structure.pin_bit_mask = (1ULL << GPIO_IN_PIN); // 引脚
    gpio_config_structure.pull_down_en = 0;                     // 禁止下拉
    gpio_config_structure.pull_up_en = 0;                       // 禁止上拉
    gpio_config_structure.intr_type = GPIO_INTR_DISABLE;        // 禁止中断

    gpio_config(&gpio_config_structure);                        // 根据设定参数初始化并使能

    while (1)
    {
        ESP_LOGI(TAG, "Input level of GPIO: %d ", gpio_get_level(GPIO_IN_PIN));
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

xQueueHandle gpio_evt_queue = NULL;      //队列

/* 中断服务函数 */
static void gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);     //传递数据到队列中
}


void Intr_gpio_isr(void *arg)
{
    uint32_t io_num;
    while(1)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            printf("GPIO[%d] intr, val: %d\n", io_num, gpio_get_level(io_num));
            if(gpio_get_level(io_num) == 0)
            {
                gpio_set_level(GPIO_OUT_PIN, 0);
            }
        }
    }
}

void fun_set_gpio_low_interrupt(void)
{
    gpio_config_t gpio_config_structure;                        // 定义一个gpio配置结构体

    // 初始化gpio配置结构体
    gpio_config_structure.mode = GPIO_MODE_INPUT;               // 输出模式
    gpio_config_structure.pin_bit_mask = (1ULL << GPIO_IN_PIN); // 引脚
    gpio_config_structure.pull_down_en = 0;                     // 禁止下拉
    gpio_config_structure.pull_up_en = 0;                       // 禁止上拉
    gpio_config_structure.intr_type = GPIO_INTR_NEGEDGE;        //下降沿触发中断
    gpio_config(&gpio_config_structure);                        // 根据设定参数初始化并使能

	
	gpio_install_isr_service(1);                                // 注册中断服务
	gpio_isr_handler_add(GPIO_IN_PIN, gpio_isr_handler,(void*) GPIO_IN_PIN);    // 设置GPIO的中断回调函数

	
	gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));        // 创建一个消息队列，从中获取队列句柄

    xTaskCreate(Intr_gpio_isr, "Intr_gpio_isr", 1024, NULL, 10, NULL);	        // 创建INTR任务 
}


void app_main()
{
	//设置gpio的电平输出
	fun_set_gpio_level();

	//获取gpio的电平输入
	//fun_get_gpio_level();

	//gpio中断
	fun_set_gpio_low_interrupt();
}


