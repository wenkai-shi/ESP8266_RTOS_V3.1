/* sniffer example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_libc.h"

#define TAG "sniffer"

#define MAC_HEADER_LEN 24
#define SNIFFER_DATA_LEN 112
#define MAC_HDR_LEN_MAX 40

static EventGroupHandle_t wifi_event_group;

static const int START_BIT = BIT0;

static char printbuf[100];

static void sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type)
{
    wifi_pkt_rx_ctrl_t* rx_ctrl = (wifi_pkt_rx_ctrl_t*)buf;
    uint8_t* frame = (uint8_t*)(rx_ctrl + 1);
    uint32_t len = rx_ctrl->sig_mode ? rx_ctrl->HT_length : rx_ctrl->legacy_length;
    uint32_t i;

    uint8_t total_num = 1, count = 0;
    uint16_t seq_buf = 0;

    if ((rx_ctrl->aggregation) && (type != WIFI_PKT_MISC)) {
        total_num = rx_ctrl->ampdu_cnt;
    }

    for (count = 0; count < total_num; count++) {
        if (total_num > 1) {
            len = *((uint16_t*)(frame + MAC_HDR_LEN_MAX + 2 * count));

            if (seq_buf == 0) {
                seq_buf = *((uint16_t*)(frame + 22)) >> 4;
            }

            ESP_LOGI(TAG, "seq_num:%d, total_num:%d\r\n", seq_buf, total_num);
        }

        switch (type) {
            case WIFI_PKT_MGMT:
                ESP_LOGI(TAG, "Rx mgmt pkt len:%d", len);
                break;

            case WIFI_PKT_CTRL:
                ESP_LOGI(TAG, "Rx ctrl pkt len:%d", len);
                break;

            case WIFI_PKT_DATA:
                ESP_LOGI(TAG, "Rx data pkt len:%d", len);
                break;

            case WIFI_PKT_MISC:
                ESP_LOGI(TAG, "Rx misc pkt len:%d", len);
                len = len > MAC_HEADER_LEN ? MAC_HEADER_LEN : len;
                break;

            default :
                len = 0;
                ESP_LOGE(TAG, "Rx unknown pkt len:%d", len);
                return;
        }

        ++seq_buf;

        if (total_num > 1) {
            *(uint16_t*)(frame + 22) = (seq_buf << 4) | (*(uint16_t*)(frame + 22) & 0xf);
        }
    }

    ESP_LOGI(TAG, "Rx ctrl header:");

    for (i = 0; i < 12; i++) {
        sprintf(printbuf + i * 3, "%02x ", *((uint8_t*)buf + i));
    }

    ESP_LOGI(TAG, "  - %s", printbuf);

    ESP_LOGI(TAG, "Data:");

    len = len > SNIFFER_DATA_LEN ? SNIFFER_DATA_LEN : len;

    for (i = 0; i < len; i++) {
        sprintf(printbuf + (i % 16) * 3, "%02x ", *((uint8_t*)frame + i));

        if ((i + 1) % 16 == 0) {
            ESP_LOGI(TAG, "  - %s", printbuf);
        }
    }

    if ((i % 16) != 0) {
        printbuf[((i) % 16) * 3 - 1] = 0;
        ESP_LOGI(TAG, "  - %s", printbuf);
    }
}

static void sniffer_task(void* pvParameters)
{
    wifi_promiscuous_filter_t sniffer_filter = {0};

#if CONFIG_FILTER_MASK_MGMT
    sniffer_filter.filter_mask |= WIFI_PROMIS_FILTER_MASK_MGMT;
#endif

#if CONFIG_FILTER_MASK_CTRL
    sniffer_filter.filter_mask |= WIFI_PROMIS_FILTER_MASK_CTRL;
#endif

#if CONFIG_FILTER_MASK_DATA
    sniffer_filter.filter_mask |= WIFI_PROMIS_FILTER_MASK_DATA;
#endif

#if CONFIG_FILTER_MASK_DATA_FRAME_PAYLOAD
    /*Enable to receive the correct data frame payload*/
    extern esp_err_t esp_wifi_set_recv_data_frame_payload(bool enable_recv);
    ESP_ERROR_CHECK(esp_wifi_set_recv_data_frame_payload(true));
#endif

#if CONFIG_FILTER_MASK_MISC
    sniffer_filter.filter_mask |= WIFI_PROMIS_FILTER_MASK_MISC;
#endif

    if (sniffer_filter.filter_mask == 0) {
        ESP_LOGI(TAG, "Please add one filter at least!");
        vTaskDelete(NULL);
    }

    // 等待事件标志位被设置
    xEventGroupWaitBits(wifi_event_group, START_BIT, false, true, portMAX_DELAY);
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_CHANNEL, 0));               // 设置ESP8266的主/从通道
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(sniffer_cb));            // 注册RX回调函数。每次收到数据包时，都会调用注册的回调函数。
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&sniffer_filter));      // 启用混杂模式包类型过滤功能。
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));                        // 启用混杂模式。
    vTaskDelete(NULL);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // 系统事件为WiFi事件 事件id为STA开始
    if (event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(wifi_event_group, START_BIT);
    }
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();                               // 初始化tcpip适配器

    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环

    wifi_event_group = xEventGroupCreate();             // 创建一个FreeRTOS事件标志组

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();// 获取一个默认的wifi配置参数结构体变量
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));               // 根据cfg参数初始化wifi连接所需要的资源

    // 将事件处理程序注册到系统默认事件循环 WiFi事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));  // 设置WiFi为STA模式
    ESP_ERROR_CHECK(esp_wifi_start());                  // 开启wifi状态机
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());      // 初始化默认的NVS分区   
    initialise_wifi();                      // 初始化WiFi
    xTaskCreate(&sniffer_task, "sniffer_task", 2048, NULL, 10, NULL);
}