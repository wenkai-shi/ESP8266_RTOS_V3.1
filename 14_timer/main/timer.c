#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/hw_timer.h"

static const char *TAG = "timer";

#define TEST_ONE_SHOT    false        // 测试将在没有自动加载的情况下完成(一次性)
#define TEST_RELOAD      true         // 测试将通过自动重新加载完成

#define GPIO_OUTPUT_IO      4
#define GPIO_OUTPUT_PIN_SEL  (1ULL << GPIO_OUTPUT_IO)

/* 回调函数 */
void hw_timer_callback1(void *arg)
{
    static int state = 0;
    gpio_set_level(GPIO_OUTPUT_IO, (state ++) % 2);
}

/* 回调函数 */
void hw_timer_callback2(void *arg)
{
    gpio_set_level(GPIO_OUTPUT_IO, 0);
}

void hw_timer_reload(void *arg)
{
    hw_timer_init(hw_timer_callback1, NULL); // 初始化硬件计时器
    while(1)
    {
        hw_timer_alarm_us(1000000, TEST_RELOAD);    // 设置一个触发计时器us延迟来启用这个计时器 周期性
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void hw_timer_one(void *arg)
{
    hw_timer_init(hw_timer_callback2, NULL); // 初始化硬件计时器
    while(1)
    {
        hw_timer_alarm_us(1000000, TEST_RELOAD);    // 设置一个触发计时器us延迟来启用这个计时器 周期性
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Config gpio");

    gpio_config_t gpio_config_structure;
    gpio_config_structure.mode = GPIO_MODE_OUTPUT;              // 输出模式
    gpio_config_structure.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;   // 引脚
    gpio_config_structure.pull_down_en = 0;                     // 禁止下拉
    gpio_config_structure.pull_up_en = 0;                       // 禁止上拉
    gpio_config_structure.intr_type = GPIO_INTR_DISABLE;        // 禁止中断
    gpio_config(&gpio_config_structure);                        // 根据设定参数初始化并使能

    xTaskCreate(hw_timer_reload, "hw_timer_reload", 1024, NULL, 10, NULL);

    //xTaskCreate(hw_timer_one, "hw_timer_one", 1024, NULL, 10, NULL);
}


