#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"

#include "esp_log.h"

static const char *TAG = "uart_echo";

/**
 * This is an example which echos any data it receives on UART0 back to the sender,
 * with hardware flow control turned off. It does not use UART driver event queue.
 *
 * - Port: UART0
 * - Receive (Rx) buffer: on
 * - Transmit (Tx) buffer: off
 * - Flow control: off
 * - Event queue: off
 */

#define BUF_SIZE (1024)

void echo_task(void *arg)
{
    // 配置UART驱动程序的参数
    // 通信引脚和安装驱动程序
    uart_config_t uart_config = {
        .baud_rate = 74880,
        .data_bits = UART_DATA_8_BITS,          // 8位数据
        .parity    = UART_PARITY_DISABLE,       // 禁止奇偶校验
        .stop_bits = UART_STOP_BITS_1,          // 1个停止位
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE   // 禁用硬件控制流
    };

    uart_param_config(UART_NUM_0, &uart_config);    // 初始化串口通用参数
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, 0);   // 安装UART驱动程序

    // 为传入数据配置一个临时缓冲区
    uint8_t data[BUF_SIZE] = {"hello, This is a piece of data...."};

    while (1) 
    {
        // 从UART读取数据
        int len = uart_read_bytes(UART_NUM_0, data, BUF_SIZE, 20 / portTICK_RATE_MS);
        if(len < 0){
            ESP_LOGI(TAG, "Reading data error");
        }
        else{
            ESP_LOGI(TAG, "Reading data succeed");
        }
        
        // 将数据写回UART
        len = uart_write_bytes(UART_NUM_0, (const char *) data, len);
        if(len < 0){
            ESP_LOGI(TAG, "Sending data error");
        }
        else{
            ESP_LOGI(TAG, "Sending data succeed");
            ESP_LOGI(TAG, "%s",data);
        }
        vTaskDelay(2000 / portTICK_RATE_MS);
    }
}

void app_main()
{
    xTaskCreate(echo_task, "uart_echo_task", 2048*2, NULL, 10, NULL);
}
