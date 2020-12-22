#ifndef __I2C_MASTER_H__
#define __I2C_MASTER_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "rom/ets_sys.h"


#define IIC_SDA_PIN             2
#define IIC_SCL_PIN             14

#define IIC_OUTPUT_PIN_SEL      ((1ULL<<IIC_SCL_PIN) | (1ULL<<IIC_SDA_PIN)) 

#define IIC_SDA_IN()            gpio_get_level(IIC_SDA_PIN)  //SDA设置为输入

//IIC_SDA IIC_SCL 输出设置
#define IIC_SCL_OUT(state)      gpio_set_level(IIC_SCL_PIN, state)
#define IIC_SDA_OUT(state)      gpio_set_level(IIC_SDA_PIN, state)

typedef enum {
    SDA_MODE_WRITE = GPIO_MODE_OUTPUT,  /*!< SDA 写模式 */
    SDA_MODE_READ = GPIO_MODE_INPUT,    /*!< SDA 读模式 */
} sda_mode_t;

void i2c_master_gpio_init(void);
void IIC_SDA_Mode_Change(uint8_t SDA_Mode);
void i2c_master_start(void);
void i2c_master_stop(void);
void i2c_master_send_ack(void);
void i2c_master_send_nack(void);
uint8_t i2c_master_Wait_Ack(void);
uint8_t i2c_master_readByte(uint8_t byte);
void i2c_master_sentByte(uint8_t wrdata);

#endif
