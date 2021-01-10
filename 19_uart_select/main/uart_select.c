#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/select.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"

static const char* TAG = "uart_select";

static void uart_select_task()
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

    uart_param_config(UART_NUM_0, &uart_config);            // 初始化串口通用参数
    uart_driver_install(UART_NUM_0, 2*1024, 0, 0, NULL, 0); // 安装UART驱动程序

    while (1) 
    {
        int fd;

        if ((fd = open("/dev/uart/0", O_RDWR)) == -1) 
        {
            ESP_LOGE(TAG, "Cannot open UART");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        esp_vfs_dev_uart_use_driver(0);     // 设置VFS使用UART驱动程序进行读写

        while (1) {
            int s;
            fd_set rfds;
            struct timeval tv = {
                .tv_sec = 5,
                .tv_usec = 0,
            };

            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);

            s = select(fd + 1, &rfds, NULL, NULL, &tv);

            if (s < 0) {
                ESP_LOGE(TAG, "Select failed: errno %d", errno);
                break;
            } 

            else if (s == 0) {
                ESP_LOGI(TAG, "Timeout has been reached and nothing has been received");
            } 
            
            else 
            {
                if (FD_ISSET(fd, &rfds)) 
                {
                    char buf;
                    if (read(fd, &buf, 1) > 0) 
                    {
                        ESP_LOGI(TAG, "Received: %c", buf);
                    } 
                    /* 注意:只有一个字符被读取，即使缓冲区包含更多。其他字符将被随后的select()调用一个接一个地读取，
                    然后该调用将立即返回，没有超时 */
                    else 
                    {
                        ESP_LOGE(TAG, "UART read error");
                        break;
                    }
                } 
                else 
                {
                    ESP_LOGE(TAG, "No FD has been set in select()");
                    break;
                }
            }
        }

        close(fd);
    }

    vTaskDelete(NULL);
}

void app_main()
{
    xTaskCreate(uart_select_task, "uart_select_task", 4*1024, NULL, 5, NULL);
}
