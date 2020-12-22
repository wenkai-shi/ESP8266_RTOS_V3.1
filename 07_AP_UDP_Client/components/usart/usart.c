#include "usart.h"
#include <stdio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

/*
ESP8266 有两个UART。UART0有TX、RX作为 系统的打印信息输出接口 和 数据收发口，
而UART1只有TX，作为 打印信息输出接口(调试用)。
*/


static void uartEventTask(void *pvParameters);

static QueueHandle_t s_uart0Queue;
static const char *TAG = "board_uart";

#define BUF_SIZE                1024        //最大缓存
#define UART_MAX_NUM_RX_BYTES   1024

void Uart_Init(void) 
{
    //下位机通讯串口设置：串口0
	uart_config_t uart_config;
	uart_config.baud_rate = 115200;  //波特率为115200
	uart_config.data_bits = UART_DATA_8_BITS; 
	uart_config.parity = UART_PARITY_DISABLE;
	uart_config.stop_bits = UART_STOP_BITS_1;
	uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	uart_param_config(UART_NUM_0, &uart_config);

    // 安装 UART 驱动程序，将 UART 设置为默认配置
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, BUF_SIZE * 2, 100, &s_uart0Queue, 0);	

    // 创建任务
    xTaskCreate(uartEventTask, "uartEventTask", 2048, NULL, 12, NULL);
}

static void uartEventTask(void *pvParameters)
{
    uart_event_t event;
    uint8_t *pTempBuf = (uint8_t *)malloc(UART_MAX_NUM_RX_BYTES);

    while (1)
	{
        // 等待UART事件
        if(xQueueReceive(s_uart0Queue, (void *)&event, (portTickType)portMAX_DELAY))
		{
            bzero(pTempBuf, UART_MAX_NUM_RX_BYTES);     //置字节字符串s的前n个字节为零且包括‘\0’

            switch(event.type)
			{
                // UART接收数据的事件
                // 我们最好快速处理数据事件，这样会有更多的数据事件
                // 其他类型的事件。如果我们在数据事件上花费太多时间，队列可能已经满了
                case UART_DATA:
                    // ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(UART_NUM_0, pTempBuf, event.size, portMAX_DELAY);
                    // uart_write_bytes(UART_NUM_0, (const char *)pTempBuf, event.size);
                    break;

                // 检测到HW FIFO溢出事件
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // 如果发生fifo溢出，应该考虑为应用程序添加流控制
                    // ISR已经重置了rx FIFO
                    // 作为一个例子，我们在这里直接刷新rx缓冲区以读取更多数据
                    uart_flush_input(UART_NUM_0);
                    xQueueReset(s_uart0Queue);
                    break;

                // UART循环缓冲区满的事件
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // 如果缓冲区满了，应该考虑增加缓冲区大小
                    // 作为一个例子，我们在这里直接刷新rx缓冲区以读取更多数据
                    uart_flush_input(UART_NUM_0);
                    xQueueReset(s_uart0Queue);
                    break;

                case UART_PARITY_ERR:
                    ESP_LOGI(TAG, "uart parity error");
                    break;

                // UART帧错误事件
                case UART_FRAME_ERR:
                    ESP_LOGI(TAG, "uart frame error");
                    break;

                // 其他
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }

    free(pTempBuf);
    pTempBuf = NULL;
    vTaskDelete(NULL);
}
