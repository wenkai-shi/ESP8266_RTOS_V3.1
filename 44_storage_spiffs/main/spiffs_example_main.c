/* SPIFFS filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "example";

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",           // 与文件系统相关联的文件路径前缀
      .partition_label = NULL,
      .max_files = 5,                   // 可以同时打开的最大文件数
      .format_if_mount_failed = true    // 如果为true，则在挂载失败时将格式化文件系统
    };
    
    // 使用上面定义的设置初始化和挂载 SPIFFS 文件系统
    // 注意:esp_vfs_spiffs_register 是一个多功能合一的方便函数
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) 
    {
        if (ret == ESP_FAIL)                // 未能装入或格式化文件系统
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } 
        else if (ret == ESP_ERR_NOT_FOUND) // 未能找到 SPIFFS 分区
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } 
        else                               // 初始化SPIFFS失败 
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    
    size_t total = 0, used = 0;
    // 为 SPIFFS 获取信息
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)  // 无法获取SPIFFS分区信息
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } 
    else 
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    // 使用POSIX和C标准库函数处理文件
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/spiffs/hello.txt", "w");  // 打开/创建一个文件
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello World!\n");       // 向文件系统(流)写入数据
    fclose(f);                          // 关闭一个流
    ESP_LOGI(TAG, "File written");

    // 重命名前检查目标文件是否存在
    struct stat st;
    if (stat("/spiffs/foo.txt", &st) == 0) {
        // 如果存在，请删除
        unlink("/spiffs/foo.txt");
    }

    // 重命名原始文件
    ESP_LOGI(TAG, "Renaming file");
    if (rename("/spiffs/hello.txt", "/spiffs/foo.txt") != 0) 
    {
        ESP_LOGE(TAG, "Rename failed");
        return;
    }

    // 打开重命名的文件以进行读取
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/spiffs/foo.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);       // 从指定的流中读取数据
    fclose(f);
    // 带换行符
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    // 所有操作完成后，卸载分区并禁用SPIFFS
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");
}
