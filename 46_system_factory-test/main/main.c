// Copyright 2019-2020 Espressif Systems (Shanghai) PTE LTD
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
#include <assert.h>
#include "esp_system.h"
#include "esp_rftest.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "FreeRTOS.h"
#include "driver/uart.h"
#include "nano_console.h"
#include "esp_libc.h"
#include "esp_task.h"

#ifndef ESP_FACTORY_TEST_EXTRA_COMPONENTS

#ifndef CONFIG_ESP_CONSOLE_UART_NUM
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#endif

#define TAG "factory-test"

static void initialize_console()
{
    // 在stdin(标准输入)上禁用缓冲
    setvbuf(stdin, NULL, _IONBF, 0);

    // 当按回车键时发送CR
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    // 将插入符号移到'\n'上下一行的开头
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

    // 配置UART
    uart_config_t uart_config = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,  // 波特率
            .data_bits = UART_DATA_8_BITS,                  // 8位数据
            .parity = UART_PARITY_DISABLE,                  // 禁止奇偶校验
            .stop_bits = UART_STOP_BITS_1,                  // 1个停止位
    };
    // 配置串口常用参数
    ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));

    // 安装用于中断驱动读写的UART驱动程序
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));

    // 告诉VFS使用UART驱动程序
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    // 将RF测试命令注册到控制台
    esp_console_register_rftest_command();

    // 初始化纳米控制台
    ESP_ERROR_CHECK(nc_init());
}

void __attribute__((weak)) app_main(void)
{
    ESP_LOGI(TAG, "SDK factory test firmware version:%s\n", esp_get_idf_version()); // 返回完整的IDF版本字符串

    initialize_console();
}

#endif /* ESP_FACTORY_TEST_EXTRA_COMPONENTS */
