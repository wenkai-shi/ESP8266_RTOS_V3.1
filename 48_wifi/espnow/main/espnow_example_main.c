/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"
#include "tcpip_adapter.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_now.h"
#include "rom/ets_sys.h"
#include "rom/crc.h"
#include "espnow_example.h"

static const char *TAG = "espnow_example";

static xQueueHandle example_espnow_queue;

static uint8_t example_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint16_t s_example_espnow_seq[EXAMPLE_ESPNOW_DATA_MAX] = { 0, 0 };

static void example_espnow_deinit(example_espnow_send_param_t *send_param);

/* 使用ESPNOW之前应该启动WiFi */
static void example_wifi_init(void)
{
    tcpip_adapter_init();                                       // 初始化tcpip适配器

    ESP_ERROR_CHECK(esp_event_loop_create_default());           // 创建默认事件循环

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();        // 获取一个默认的wifi配置参数结构体变量
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );                     // 根据cfg参数初始化wifi连接所需要的资源

    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );  // 设置WiFi API配置存储类型 所有配置将只存储在内存中
    ESP_ERROR_CHECK( esp_wifi_set_mode(ESPNOW_WIFI_MODE) );     // 设置WiFi为 soft-AP 模式 
    ESP_ERROR_CHECK( esp_wifi_start());                         // 启动WiFi

    /* In order to simplify example, channel is set after WiFi started.
     * This is not necessary in real application if the two devices have
     * been already on the same channel.
     */
    ESP_ERROR_CHECK( esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, 0) );  // 设置ESP8266的主/从通道
}

/* WiFi任务调用ESPNOW发送或接收回调函数。用户不应该在此任务中执行冗长的操作。
 * 相反，向队列提交必要的数据，并从较低优先级的任务处理它 */
static void example_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    example_espnow_event_t evt;     // 当ESPNOW发送或接收回调函数被调用时，post事件到ESPNOW任务
    example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_SEND_CB;
    memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    send_cb->status = status;
    // 向队列发送消息 最终调用xQueueGenericSend
    if (xQueueSend(example_espnow_queue, &evt, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send send queue fail");
    }
}

static void example_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    example_espnow_event_t evt;
    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

    if (mac_addr == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    evt.id = EXAMPLE_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL) {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;
    // 向队列发送消息 最终调用xQueueGenericSend
    if (xQueueSend(example_espnow_queue, &evt, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

/* 解析接收到的ESPNOW数据 */
int example_espnow_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state, uint16_t *seq, int *magic)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(example_espnow_data_t)) {
        ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    crc = buf->crc;
    buf->crc = 0;
    crc_cal = crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return buf->type;
    }

    return -1;
}

/* 准备发送ESPNOW数据 */
void example_espnow_data_prepare(example_espnow_send_param_t *send_param)
{
    example_espnow_data_t *buf = (example_espnow_data_t *)send_param->buffer;
    int i = 0;

    assert(send_param->len >= sizeof(example_espnow_data_t));

    buf->type = IS_BROADCAST_ADDR(send_param->dest_mac) ? EXAMPLE_ESPNOW_DATA_BROADCAST : EXAMPLE_ESPNOW_DATA_UNICAST;
    buf->state = send_param->state;
    buf->seq_num = s_example_espnow_seq[buf->type]++;
    buf->crc = 0;
    buf->magic = send_param->magic;
    for (i = 0; i < send_param->len - sizeof(example_espnow_data_t); i++) {
        buf->payload[i] = (uint8_t)esp_random();
    }
    buf->crc = crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

static void example_espnow_task(void *pvParameter)
{
    example_espnow_event_t evt;     // 当ESPNOW发送或接收回调函数被调用时，post事件到ESPNOW任务
    uint8_t recv_state = 0;
    uint16_t recv_seq = 0;
    int recv_magic = 0;
    bool is_broadcast = false;
    int ret;

    vTaskDelay(5000 / portTICK_RATE_MS);
    ESP_LOGI(TAG, "Start sending broadcast data");

    // 开始发送广播ESPNOW数据
    example_espnow_send_param_t *send_param = (example_espnow_send_param_t *)pvParameter;
    // 发送ESPNOW数据
    if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) 
    {
        ESP_LOGE(TAG, "Send error");
        example_espnow_deinit(send_param);
        vTaskDelete(NULL);
    }

    // 从队列接收一个项目
    while (xQueueReceive(example_espnow_queue, &evt, portMAX_DELAY) == pdTRUE) 
    {
        switch (evt.id) 
        {
            case EXAMPLE_ESPNOW_SEND_CB:
            {
                example_espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
                is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

                ESP_LOGD(TAG, "Send data to "MACSTR", status1: %d", MAC2STR(send_cb->mac_addr), send_cb->status);

                if (is_broadcast && (send_param->broadcast == false)) {
                    break;
                }

                if (!is_broadcast) 
                {
                    send_param->count--;
                    if (send_param->count == 0) {
                        ESP_LOGI(TAG, "Send done");
                        example_espnow_deinit(send_param);
                        vTaskDelete(NULL);
                    }
                }

                // 延迟一段时间后再发送下一个数据
                if (send_param->delay > 0) {
                    vTaskDelay(send_param->delay/portTICK_RATE_MS);
                }

                ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(send_cb->mac_addr));

                memcpy(send_param->dest_mac, send_cb->mac_addr, ESP_NOW_ETH_ALEN);
                // 准备发送ESPNOW数据
                example_espnow_data_prepare(send_param);

                // 在发送前一个数据后再发送下一个数据
                if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) {
                    ESP_LOGE(TAG, "Send error");
                    example_espnow_deinit(send_param);
                    vTaskDelete(NULL);
                }
                break;
            }
            case EXAMPLE_ESPNOW_RECV_CB:
            {
                example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

                // 解析接收到的ESPNOW数据
                ret = example_espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state, &recv_seq, &recv_magic);
                free(recv_cb->data);
                if (ret == EXAMPLE_ESPNOW_DATA_BROADCAST) 
                {
                    ESP_LOGI(TAG, "Receive %dth broadcast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                    // 如果“对等体列表”中没有MAC地址，则将其添加到“对等体列表”中
                    if (esp_now_is_peer_exist(recv_cb->mac_addr) == false) 
                    {
                        esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
                        if (peer == NULL) {
                            ESP_LOGE(TAG, "Malloc peer information fail");
                            example_espnow_deinit(send_param);
                            vTaskDelete(NULL);
                        }
                        memset(peer, 0, sizeof(esp_now_peer_info_t));
                        peer->channel = CONFIG_ESPNOW_CHANNEL;      // peer用来发送/接收ESPNOW数据的Wi-Fi通道
                        peer->ifidx = ESPNOW_WIFI_IF;               // Wi-Fi接口，用于发送/接收ESPNOW数据
                        peer->encrypt = true;                      // ESPNOW这个对等体发送/接收的数据是否加密
                        memcpy(peer->lmk, CONFIG_ESPNOW_LMK, ESP_NOW_KEY_LEN);
                        memcpy(peer->peer_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                        // 添加对等体到对等体列表
                        ESP_ERROR_CHECK( esp_now_add_peer(peer) );
                        free(peer);
                    }

                    // 表示设备已经接收到广播ESPNOW数据
                    if (send_param->state == 0) {
                        send_param->state = 1;
                    }

                    /* 如果接收广播ESPNOW数据，表明另一个设备已经接收到广播ESPNOW数据，
                     * 本地随机数字大于接收广播ESPNOW数据，停止发送广播ESPNOW数据，并开始发送单播ESPNOW数据。
                     */
                    if (recv_state == 1) 
                    {
                        // 有较大随机数的设备发送ESPNOW数据，另一个接收ESPNOW数据。
                        if (send_param->unicast == false && send_param->magic >= recv_magic) 
                        {
                    	    ESP_LOGI(TAG, "Start sending unicast data");
                    	    ESP_LOGI(TAG, "send data to "MACSTR"", MAC2STR(recv_cb->mac_addr));

                    	    // 开始发送单播ESPNOW数据
                            memcpy(send_param->dest_mac, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
                            // 准备发送ESPNOW数据
                            example_espnow_data_prepare(send_param);
                            // 发送ESPNOW数据
                            if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) != ESP_OK) 
                            {
                                ESP_LOGE(TAG, "Send error");
                                example_espnow_deinit(send_param);
                                vTaskDelete(NULL);
                            }
                            else {
                                send_param->broadcast = false;
                                send_param->unicast = true;
                            }
                        }
                    }
                }
                else if (ret == EXAMPLE_ESPNOW_DATA_UNICAST) 
                {
                    ESP_LOGI(TAG, "Receive %dth unicast data from: "MACSTR", len: %d", recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

                    // 如果接收单播ESPNOW数据，也停止发送广播ESPNOW数据
                    send_param->broadcast = false;
                }
                else {
                    ESP_LOGI(TAG, "Receive error data from: "MACSTR"", MAC2STR(recv_cb->mac_addr));
                }
                break;
            }
            default:
                ESP_LOGE(TAG, "Callback type error: %d", evt.id);
                break;
        }
    }
}

static esp_err_t example_espnow_init(void)
{
    example_espnow_send_param_t *send_param;

    // 创建一个新的队列。为新的队列分配所需的存储内存，并返回一个队列处理
    example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (example_espnow_queue == NULL) {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    // 初始化ESPNOW并注册发送和接收回调函数
    ESP_ERROR_CHECK( esp_now_init() );
    ESP_ERROR_CHECK( esp_now_register_send_cb(example_espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(example_espnow_recv_cb) );

    // 设置主密钥
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK) );

    // 将广播对等体信息添加到对等体列表中
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));    // 动态申请ESPNOW对等体结构体空间大小
    if (peer == NULL) {
        ESP_LOGE(TAG, "Malloc peer information fail");
        vSemaphoreDelete(example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;      // peer用来发送/接收ESPNOW数据的Wi-Fi通道
    peer->ifidx = ESPNOW_WIFI_IF;               // Wi-Fi接口，用于发送/接收ESPNOW数据
    peer->encrypt = false;                      // ESPNOW这个对等体发送/接收的数据是否加密
    memcpy(peer->peer_addr, example_broadcast_mac, ESP_NOW_ETH_ALEN);
    // 添加对等体到对等体列表
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    /* 初始化发送参数 */
    send_param = malloc(sizeof(example_espnow_send_param_t));       // 动态申请发送ESPNOW数据的参数结构体空间大小
    memset(send_param, 0, sizeof(example_espnow_send_param_t));
    if (send_param == NULL) {
        ESP_LOGE(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    send_param->unicast = false;                        // 发送单播ESPNOW数据
    send_param->broadcast = true;                       // 发送广播ESPNOW数据
    send_param->state = 0;                              // 表示是否接收到广播ESPNOW数据
    send_param->magic = esp_random();                   // 用于确定发送单播ESPNOW数据的设备的随机数字
    send_param->count = CONFIG_ESPNOW_SEND_COUNT;       // 发送ESPNOW单播数据的总数
    send_param->delay = CONFIG_ESPNOW_SEND_DELAY;       // 发送两个ESPNOW数据之间的延迟，单位:ms
    send_param->len = CONFIG_ESPNOW_SEND_LEN;           // 发送ESPNOW数据的长度，单位byte
    send_param->buffer = malloc(CONFIG_ESPNOW_SEND_LEN);// 指向ESPNOW数据的缓冲区
    if (send_param->buffer == NULL) {
        ESP_LOGE(TAG, "Malloc send buffer fail");
        free(send_param);
        vSemaphoreDelete(example_espnow_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memcpy(send_param->dest_mac, example_broadcast_mac, ESP_NOW_ETH_ALEN);
    // 准备发送ESPNOW数据
    example_espnow_data_prepare(send_param);

    xTaskCreate(example_espnow_task, "example_espnow_task", 2048, send_param, 4, NULL);

    return ESP_OK;
}

static void example_espnow_deinit(example_espnow_send_param_t *send_param)
{
    free(send_param->buffer);
    free(send_param);
    vSemaphoreDelete(example_espnow_queue);
    esp_now_deinit();
}

void app_main()
{
    ESP_ERROR_CHECK( nvs_flash_init() );    // 初始化默认的NVS分区

    example_wifi_init();                    // 使用ESPNOW之前应该启动WiFi
    example_espnow_init();                  // 初始化ESPNOW
}
