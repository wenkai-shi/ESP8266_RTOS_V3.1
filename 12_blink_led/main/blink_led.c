#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"


static const char *TAG = "blink_led";

//宏定义
#define BLINK_GPIO 4

void blink_task(void *pvParameter)
{
    //设置为输出
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    while(1) {
        //设置低电平
        gpio_set_level(BLINK_GPIO, 0);
		//延时1s
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        //设置高电平
        gpio_set_level(BLINK_GPIO, 1);
		//延时1s
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main()
{
    xTaskCreate(&blink_task, "blink_task", 1024, NULL, 5, NULL);
}


