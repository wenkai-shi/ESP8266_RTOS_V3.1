#include "key.h"
#include "driver/gpio.h"

void KEY_Init(void)
{
    /* 定义一个gpio配置结构体 */
    gpio_config_t gpio_config_structure;

    /* 初始化gpio配置结构体*/
    gpio_config_structure.mode = GPIO_MODE_INPUT;                   //输入模式
    gpio_config_structure.pin_bit_mask = (1ULL << KEY_DATA_PIN);    //引脚
    gpio_config_structure.pull_down_en = 0;                         //禁止下拉
    gpio_config_structure.pull_up_en = 0;                           //禁止上拉
    gpio_config_structure.intr_type = GPIO_INTR_DISABLE;            //禁止中断

    /* 根据设定参数初始化并使能 */
    gpio_config(&gpio_config_structure);
}
