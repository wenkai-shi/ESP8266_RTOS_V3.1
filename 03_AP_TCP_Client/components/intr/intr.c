#include "intr.h"
#include "led.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "driver/gpio.h"

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
                gpio_set_level(LED_DATA_PIN, 0);
            }
        }
    }
}

void Intr_Init(void)
{
    /* 定义一个gpio配置结构体 */
    gpio_config_t gpio_config_structure;

    /* 初始化gpio配置结构体*/
    gpio_config_structure.mode = GPIO_MODE_INPUT;                   //输入模式
    gpio_config_structure.pin_bit_mask = (1ULL << INTR_DATA_PIN);   //引脚
    gpio_config_structure.pull_down_en = 0;                         //禁止下拉
    gpio_config_structure.pull_up_en = 1;                           //上拉
    gpio_config_structure.intr_type = GPIO_INTR_NEGEDGE;            //下降沿触发中断
    gpio_config(&gpio_config_structure);                            //根据设定参数初始化并使能


    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));                    //创建一个队列来处理isr中的gpio事件
    	
	xTaskCreate(Intr_gpio_isr, "Intr_gpio_isr", 1024, NULL, 10, NULL);	    //创建INTR任务 

    gpio_install_isr_service(0);                                            //注册中断服务函数
    gpio_isr_handler_add(INTR_DATA_PIN, (void *)gpio_isr_handler, (void *)INTR_DATA_PIN);    //添加 INTR_DATA_PIN 的中断回调函数
}


