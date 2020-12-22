#ifndef _DHT11_
#define _DHT11_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <unistd.h>
#include "esp_log.h"
#include "esp_err.h"

#define DTH11_DATA_PIN          5
#define GPIO_OUTPUT_PIN_SEL     1ULL<<DTH11_DATA_PIN

void DHT11_init(void);
int get_dth_data(int *dtemperature, int *dhumidty);

#endif
