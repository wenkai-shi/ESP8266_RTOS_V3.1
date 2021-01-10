/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

static const char *TAG = "simple_ota_example";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) 
    {
        // 执行过程中发生错误
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;

        // 一但HTTP连接到服务器，就不会进行数据交换
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;

        
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;

        // 在接收从服务器发送的每个报头时发生
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;

        // 当从服务器接收数据时发生，可能是数据包的多个部分
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;

        // 在完成HTTP会话时发生
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;

        // 断开连接
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

void simple_ota_example_task(void * pvParameter)
{
    ESP_LOGI(TAG, "Starting OTA example...");

    esp_http_client_config_t config = {
        .url = CONFIG_FIRMWARE_UPGRADE_URL,         // HTTP URL，关于URL的信息是最重要的，它覆盖下面的其他字段(如果有的话)
        .cert_pem = (char *)server_cert_pem_start,  // SSL服务器认证，PEM格式为字符串，如果客户端需要验证服务器
        .event_handler = _http_event_handler,       // HTTP事件处理
    };
    // HTTPS OTA固件升级
    esp_err_t ret = esp_https_ota(&config);
    if (ret == ESP_OK) {
        // 重启CPU
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware Upgrades Failed");
    }
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());                      // 初始化默认的NVS分区 
    ESP_ERROR_CHECK(esp_netif_init());                      // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());       // 创建默认事件循环

    ESP_ERROR_CHECK(example_connect());                     // 配置Wi-Fi或以太网，连接，等待IP

    xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 5, NULL);
}
