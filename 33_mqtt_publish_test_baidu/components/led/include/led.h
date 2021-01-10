#ifndef _LED_
#define _LED_

#include "driver/gpio.h"

#define LED_DATA_PIN          4

#define LED_ON  gpio_set_level(LED_DATA_PIN, 0)        /* 点亮 */
#define LED_OFF gpio_set_level(LED_DATA_PIN, 1)        /* 熄灭 */

void LED_Init(void);

#endif
