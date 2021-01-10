/* Iperf Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <errno.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"


#define WIFI_CONNECTED_BIT BIT0

static void initialize_console(void)
{
    // 在stdin(标准输入)上禁用缓冲
    setvbuf(stdin, NULL, _IONBF, 0);

    // 当按回车键时发送CR
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    // 将插入符号移到'\n'上下一行的开头
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

    // 安装用于中断驱动读写的UART驱动程序
    ESP_ERROR_CHECK( uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0) );

    // 告诉VFS使用UART驱动程序
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    // 初始化控制台
    esp_console_config_t console_config = {
        .max_cmdline_args = 32,             // 要解析的命令行参数的最大数目
        .max_cmdline_length = 256,          // 命令行缓冲区的长度，以字节为单位
#if CONFIG_LOG_COLORS
        .hint_color = atoi(LOG_COLOR_CYAN)  // 提示文字的ASCII颜色码
#endif
    };
    ESP_ERROR_CHECK( esp_console_init(&console_config) );

    // 配置linenoise行完成库
    // 启用多行编辑。如果没有设置，长命令将在里面滚动一行。
    linenoiseSetMultiLine(1);

    // 告诉linenoise在哪里可以得到命令补全和提示
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback *) &esp_console_get_hint);

    // 设置命令历史记录大小
    linenoiseHistorySetMaxLen(100);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();       // 初始化默认的NVS分区 
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    initialise_wifi();
    initialize_console();

    // 寄存器的命令
    // 默认的“帮助”命令打印已注册命令的列表以及提示和帮助字符串
    esp_console_register_help_command();
    register_system();      // 注册系统功能
    register_wifi();        // 注册WiFi功能

    // 每一行之前打印的提示符 esp8266>
    const char *prompt = LOG_COLOR_I "esp8266> " LOG_RESET_COLOR;

    printf("\n ==================================================\n");
    printf(" |       Steps to test WiFi throughput            |\n");
    printf(" |                                                |\n");
    printf(" |  1. Print 'help' to gain overview of commands  |\n");
    printf(" |  2. Configure device to station or soft-AP     |\n");
    printf(" |  3. Setup WiFi connection                      |\n");
    printf(" |  4. Run iperf to test UDP/TCP RX/TX throughput |\n");
    printf(" |                                                |\n");
    printf(" =================================================\n\n");

    // 判断终端是否支持转义序列
    int probe_status = linenoiseProbe();
    if (probe_status)   // 零表示成功
    {
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead.\n");

        // 设置如果终端不能识别转义序列
        linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
        /* Since the terminal doesn't support escape sequences,
         * don't use color codes in the prompt.
         */
        prompt = "esp8266> ";
#endif //CONFIG_LOG_COLORS
    }

    while(true) 
    {
        // 使用linenoise获取一条线， 回车返回
        char* line = linenoise(prompt);
        // 忽略空行
        if (line == NULL) 
        {
            continue;
        }
        // 将命令添加到历史记录中
        linenoiseHistoryAdd(line);

        // 尝试运行命令
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        // 未找到请求的资源
        if (err == ESP_ERR_NOT_FOUND) 
        {
            printf("Unrecognized command\n");
        } 
        else if (err == ESP_OK && ret != ESP_OK) 
        {
            printf("Command returned non-zero error code: 0x%x\n", ret);
        } 
        else if (err != ESP_OK) 
        {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        // linenoise在堆上分配行缓冲区，所以需要释放它
        linenoiseFree(line);
    }
}

