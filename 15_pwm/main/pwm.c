#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "driver/gpio.h"
#include "driver/pwm.h"

static const char *TAG = "pwm";

#define PWM_0_OUT_IO_NUM   12       // 没用到
#define PWM_1_OUT_IO_NUM   13       // 没用到
#define PWM_2_OUT_IO_NUM   14       // 没用到
#define PWM_3_OUT_IO_NUM   4        // 用到的

// PWM周期1000us(1Khz)，与深度相同
#define PWM_PERIOD    (1000)

// pwm管脚
const uint32_t pin_num[4] = {
    PWM_0_OUT_IO_NUM,
    PWM_1_OUT_IO_NUM,
    PWM_2_OUT_IO_NUM,
    PWM_3_OUT_IO_NUM
};

// 占空比, real_duty = duties[x]/PERIOD
uint32_t duties[4] = {
    500, 500, 500, 500,
};

// 相位, delay = (phase[x]/360)*PERIOD
float phase[4] = {
    0, 0, 90.0, -90.0,
};

void app_main()
{
    pwm_init(PWM_PERIOD, duties, 4, pin_num);   // 设置周期、占空比、通道数、通道的GPIO
    pwm_set_phases(phase);                      // 设置相位：使输出达到最大功率
    pwm_start();                                // PWM 更新参数
    int16_t count = 0;

    while (1) {
        if (count == 20) {
            // channel0, 1 output hight level.
            // channel2, 3 output low level.
            pwm_stop(0x3);                      // 停止所有PWM通道
            ESP_LOGI(TAG, "PWM stop\n");
        } 
        else if (count == 30) {
            pwm_start();                        // 开始PWM通道
            ESP_LOGI(TAG, "PWM re-start\n");
            count = 0;
        }

        count++;
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}


