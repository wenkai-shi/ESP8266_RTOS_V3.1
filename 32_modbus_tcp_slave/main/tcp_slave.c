#include <stdio.h>
#include "esp_err.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#ifdef CONFIG_MB_MDNS_IP_RESOLVER
#include "mdns.h"
#endif
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "mbcontroller.h"       // 用于mbcontroller的定义和api
#include "modbus_params.h"      // 对于modbus参数结构

#define MB_TCP_PORT_NUMBER      (CONFIG_FMB_TCP_PORT_DEFAULT)
#define MB_MDNS_PORT            (502)

// 下面的定义用于定义每种Modbus寄存器的寄存器起始地址
#define MB_REG_DISCRETE_INPUT_START         (0x0000)
#define MB_REG_INPUT_START                  (0x0000)
#define MB_REG_HOLDING_START                (0x0000)
#define MB_REG_COILS_START                  (0x0000)

#define MB_PAR_INFO_GET_TOUT                (50) // 获取参数信息的超时时间
#define MB_CHAN_DATA_MAX_VAL                (10)
#define MB_CHAN_DATA_OFFSET                 (1.1f)

#define MB_READ_MASK                        (MB_EVENT_INPUT_REG_RD \
                                                | MB_EVENT_HOLDING_REG_RD \
                                                | MB_EVENT_DISCRETE_RD \
                                                | MB_EVENT_COILS_RD)
#define MB_WRITE_MASK                       (MB_EVENT_HOLDING_REG_WR \
                                                | MB_EVENT_COILS_WR)
#define MB_READ_WRITE_MASK                  (MB_READ_MASK | MB_WRITE_MASK)

#define SLAVE_TAG "SLAVE_TEST"

#if CONFIG_MB_MDNS_IP_RESOLVER

#define MB_ID_BYTE0(id) ((uint8_t)(id))
#define MB_ID_BYTE1(id) ((uint8_t)(((uint16_t)(id) >> 8) & 0xFF))
#define MB_ID_BYTE2(id) ((uint8_t)(((uint32_t)(id) >> 16) & 0xFF))
#define MB_ID_BYTE3(id) ((uint8_t)(((uint32_t)(id) >> 24) & 0xFF))

#define MB_ID2STR(id) MB_ID_BYTE0(id), MB_ID_BYTE1(id), MB_ID_BYTE2(id), MB_ID_BYTE3(id)

#if CONFIG_FMB_CONTROLLER_SLAVE_ID_SUPPORT
#define MB_DEVICE_ID (uint32_t)CONFIG_FMB_CONTROLLER_SLAVE_ID
#endif

#define MB_SLAVE_ADDR (CONFIG_MB_SLAVE_ADDR)

#define MB_MDNS_INSTANCE(pref) pref"mb_slave_tcp"

// convert mac from binary format to string
static inline char* gen_mac_str(const uint8_t* mac, char* pref, char* mac_str)
{
    sprintf(mac_str, "%s%02X%02X%02X%02X%02X%02X", pref, MAC2STR(mac));
    return mac_str;
}

static inline char* gen_id_str(char* service_name, char* slave_id_str)
{
    sprintf(slave_id_str, "%s%02X%02X%02X%02X", service_name, MB_ID2STR(MB_DEVICE_ID));
    return slave_id_str;
}

static inline char* gen_host_name_str(char* service_name, char* name)
{
    sprintf(name, "%s_%02X", service_name, MB_SLAVE_ADDR);
    return name;
}

static void start_mdns_service()
{
    char temp_str[32] = {0};
    uint8_t sta_mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(sta_mac, ESP_MAC_WIFI_STA));
    char* hostname = gen_host_name_str(MB_MDNS_INSTANCE(""), temp_str);
    //initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_LOGI(SLAVE_TAG, "mdns hostname set to: [%s]", hostname);

    //set default mDNS instance name
    ESP_ERROR_CHECK(mdns_instance_name_set(MB_MDNS_INSTANCE("esp32_")));

    //structure with TXT records
    mdns_txt_item_t serviceTxtData[] = {
        {"board","esp32"}
    };

    //initialize service
    ESP_ERROR_CHECK(mdns_service_add(hostname, "_modbus", "_tcp", MB_MDNS_PORT, serviceTxtData, 1));
    //add mac key string text item
    ESP_ERROR_CHECK(mdns_service_txt_item_set("_modbus", "_tcp", "mac", gen_mac_str(sta_mac, "\0", temp_str)));
    //add slave id key txt item
    ESP_ERROR_CHECK( mdns_service_txt_item_set("_modbus", "_tcp", "mb_id", gen_id_str("\0", temp_str)));
}

#endif

// 将寄存器值设置为已知状态
static void setup_reg_data(void)
{
    // 定义参数的初始状态
    discrete_reg_params.discrete_input1 = 1;
    discrete_reg_params.discrete_input3 = 1;
    discrete_reg_params.discrete_input5 = 1;
    discrete_reg_params.discrete_input7 = 1;

    holding_reg_params.holding_data0 = 1.34;
    holding_reg_params.holding_data1 = 2.56;
    holding_reg_params.holding_data2 = 3.78;
    holding_reg_params.holding_data3 = 4.90;

    coil_reg_params.coils_port0 = 0x55;
    coil_reg_params.coils_port1 = 0xAA;

    input_reg_params.input_data0 = 1.12;
    input_reg_params.input_data1 = 2.34;
    input_reg_params.input_data2 = 3.56;
    input_reg_params.input_data3 = 4.78;
}

/* Modbus slave的一个应用示例。它是基于freemodbus栈的。
 * 查看deviceparams.h文件获取有关Modbus参数分配的更多信息。
 * 这些参数可以从主应用程序访问，也可以修改
 * 通过外部Modbus主主机。
 */
void app_main(void)
{
    esp_err_t result = nvs_flash_init();                // 初始化默认的NVS分区 
    if (result == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    esp_netif_init();                                   // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环

#if CONFIG_MB_MDNS_IP_RESOLVER
    start_mdns_service();
#endif

    ESP_ERROR_CHECK(example_connect());                 // 配置Wi-Fi或以太网，连接，等待IP

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));     // 设置当前省电类型

    // 设置UART日志级别
    esp_log_level_set(SLAVE_TAG, ESP_LOG_INFO);
    void* mbc_slave_handler = NULL;

    ESP_ERROR_CHECK(mbc_slave_init_tcp(&mbc_slave_handler)); // 初始化Modbus控制器
    
    mb_param_info_t reg_info;               // 保持Modbus注册访问信息
    mb_register_area_descriptor_t reg_area; // Modbus寄存器区域描述符结构
    
    mb_communication_info_t comm_info = { 0 };
    comm_info.ip_port = MB_TCP_PORT_NUMBER;                 // TCP端口号示例
#if !CONFIG_EXAMPLE_CONNECT_IPV6
    comm_info.ip_addr_type = MB_IPV4;                       // 网络通讯协议地址类型 IPV4
#else
    comm_info.ip_addr_type = MB_IPV6;
#endif
    comm_info.ip_mode = MB_MODE_TCP;                        // TCP通信方式
    comm_info.ip_addr = NULL;                               // Modbus地址
    void * nif = NULL;
    ESP_ERROR_CHECK(tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_STA, &nif));   // 获取分配给接口的LwIP netif*
    comm_info.ip_netif_ptr = nif;                           // 网络通讯协议的网络接口
    // 设置通信参数并启动堆栈
    ESP_ERROR_CHECK(mbc_slave_setup((void*)&comm_info));

    /* 下面的代码初始化Modbus寄存器区域描述符
     * 用于Modbus保持寄存器，输入寄存器，线圈和离散输入
     * 初始化应该为每个支持的Modbus寄存器区域根据寄存器映射。
     * 当外部主服务器试图访问未初始化的区域中的寄存器时
     * 通过mbc_slave_set_descriptor() API调用Modbus栈
     * 为该寄存器区域发送异常响应。
     */
    // 初始化保持寄存器
    reg_area.type = MB_PARAM_HOLDING;                   // 设置寄存器类型 保持寄存器
    reg_area.start_offset = MB_REG_HOLDING_START;       // Modbus协议中寄存器区域的偏移量
    reg_area.address = (void*)&holding_reg_params;      // 设置指针到存储实例
    reg_area.size = sizeof(holding_reg_params);         // 设置寄存器存储实例的大小
    // 设置Modbus区域描述符
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));

    // 初始化输入寄存器
    reg_area.type = MB_PARAM_INPUT;
    reg_area.start_offset = MB_REG_INPUT_START;
    reg_area.address = (void*)&input_reg_params;
    reg_area.size = sizeof(input_reg_params);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));

    // 初始化线圈寄存器
    reg_area.type = MB_PARAM_COIL;
    reg_area.start_offset = MB_REG_COILS_START;
    reg_area.address = (void*)&coil_reg_params;
    reg_area.size = sizeof(coil_reg_params);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));

    // 初始化离散输入寄存器
    reg_area.type = MB_PARAM_DISCRETE;
    reg_area.start_offset = MB_REG_DISCRETE_INPUT_START;
    reg_area.address = (void*)&discrete_reg_params;
    reg_area.size = sizeof(discrete_reg_params);
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(reg_area));

    setup_reg_data(); // 将值设置为已知状态

    // modbus控制器和堆栈的启动
    ESP_ERROR_CHECK(mbc_slave_start());

    ESP_LOGI(SLAVE_TAG, "Modbus slave stack initialized.");
    ESP_LOGI(SLAVE_TAG, "Start modbus test...");
    // 下面的循环将在参数holding_data0时终止
    // increment每个访问周期达到CHAN_DATA_MAX_VAL的值.
    for(;holding_reg_params.holding_data0 < MB_CHAN_DATA_MAX_VAL;) 
    {
        // 检查Modbus master的读/写事件的某些事件
        mb_event_group_t event = mbc_slave_check_event(MB_READ_WRITE_MASK);                 // 在参数更改时等待特定的事件
        const char* rw_str = (event & MB_READ_MASK) ? "READ" : "WRITE";
        // 过滤事件并相应地处理它们
        if(event & (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD)) 
        {
            // 从参数队列获取参数信息
            ESP_ERROR_CHECK(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT));     // 得到参数信息
            ESP_LOGI(SLAVE_TAG, "HOLDING %s (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
                                rw_str,
                                (uint32_t)reg_info.time_stamp,
                                (uint32_t)reg_info.mb_offset,
                                (uint32_t)reg_info.type,
                                (uint32_t)reg_info.address,
                                (uint32_t)reg_info.size);
            if (reg_info.address == (uint8_t*)&holding_reg_params.holding_data0)
            {
                portENTER_CRITICAL();
                holding_reg_params.holding_data0 += MB_CHAN_DATA_OFFSET;
                if (holding_reg_params.holding_data0 >= (MB_CHAN_DATA_MAX_VAL - MB_CHAN_DATA_OFFSET)) 
                {
                    coil_reg_params.coils_port1 = 0xFF;
                }
                portEXIT_CRITICAL();
            }
        } 
        else if (event & MB_EVENT_INPUT_REG_RD) 
        {
            ESP_ERROR_CHECK(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT)); // 得到参数信息
            ESP_LOGI(SLAVE_TAG, "INPUT READ (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
                                (uint32_t)reg_info.time_stamp,
                                (uint32_t)reg_info.mb_offset,
                                (uint32_t)reg_info.type,
                                (uint32_t)reg_info.address,
                                (uint32_t)reg_info.size);
        } 
        else if (event & MB_EVENT_DISCRETE_RD) 
        {
            ESP_ERROR_CHECK(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT)); // 得到参数信息
            ESP_LOGI(SLAVE_TAG, "DISCRETE READ (%u us): ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
                                (uint32_t)reg_info.time_stamp,
                                (uint32_t)reg_info.mb_offset,
                                (uint32_t)reg_info.type,
                                (uint32_t)reg_info.address,
                                (uint32_t)reg_info.size);
        } 
        else if (event & (MB_EVENT_COILS_RD | MB_EVENT_COILS_WR)) 
        {
            ESP_ERROR_CHECK(mbc_slave_get_param_info(&reg_info, MB_PAR_INFO_GET_TOUT)); // 得到参数信息
            ESP_LOGI(SLAVE_TAG, "COILS %s (%u us), ADDR:%u, TYPE:%u, INST_ADDR:0x%.4x, SIZE:%u",
                                rw_str,
                                (uint32_t)reg_info.time_stamp,
                                (uint32_t)reg_info.mb_offset,
                                (uint32_t)reg_info.type,
                                (uint32_t)reg_info.address,
                                (uint32_t)reg_info.size);
            if (coil_reg_params.coils_port1 == 0xFF) break;
        }
    }
    // Modbus控制器报警损坏
    ESP_LOGI(SLAVE_TAG,"Modbus controller destroyed.");
    vTaskDelay(100);
    // 销毁Modbus控制器和堆栈
    ESP_ERROR_CHECK(mbc_slave_destroy());
#if CONFIG_MB_MDNS_IP_RESOLVER
    mdns_free();
#endif
}
