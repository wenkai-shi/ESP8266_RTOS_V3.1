// Copyright 2018-2025 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"


static const char *TAG = "uart_events";

/**
 * This example shows how to use the UART driver to handle special UART events.
 *
 * It also reads data from UART0 directly, and echoes it to console.
 *
 * - Port: UART0
 * - Receive (Rx) buffer: on
 * - Transmit (Tx) buffer: on
 * - Flow control: off
 * - Event queue: on
 * - Pin assignment: TxD (default), RxD (default)
 */

#define EX_UART_NUM UART_NUM_0

#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
static QueueHandle_t uart0_queue;

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t *dtmp = (uint8_t *) malloc(RD_BUF_SIZE);

    while (1)
    {
        // 等待UART事件
        if (xQueueReceive(uart0_queue, (void *)&event, (portTickType)portMAX_DELAY)) 
        {
            bzero(dtmp, RD_BUF_SIZE);
            ESP_LOGI(TAG, "uart[%d] event:", EX_UART_NUM);

            switch (event.type) {
                // UART接收数据的事件
                // 最好快速处理数据事件，否则会有更多的数据事件
                // 其他类型的事件。如果在数据事件上花费太多时间，队列可能已经满了。
                case UART_DATA:
                    ESP_LOGI(TAG, "[UART DATA]: %d", event.size);
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    ESP_LOGI(TAG, "[DATA EVT]:");
                    uart_write_bytes(EX_UART_NUM, (const char *) dtmp, event.size);
                    break;

                // 检测到HW FIFO溢出事件
                case UART_FIFO_OVF:
                    ESP_LOGI(TAG, "hw fifo overflow");
                    // 如果fifo溢出，应该考虑为你的应用程序添加流控制。
                    // ISR已经重置rx FIFO，
                    // 例如，为了读取更多数据，直接刷新rx缓冲区。
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;

                // UART环缓冲区已满事件
                case UART_BUFFER_FULL:
                    ESP_LOGI(TAG, "ring buffer full");
                    // 如果缓冲区满了，应该考虑增加缓冲区大小
                    // 例如，为了读取更多数据，我们直接刷新rx缓冲区。
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
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

    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

void app_main()
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

    uart_param_config(EX_UART_NUM, &uart_config);    // 初始化串口通用参数
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 100, &uart0_queue, 0); // 安装UART驱动程序，并获得队列

    // 创建一个任务来处理来自ISR的UART事件
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);
}