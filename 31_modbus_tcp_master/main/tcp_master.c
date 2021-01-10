// Copyright 2016-2019 Espressif Systems (Shanghai) PTE LTD
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

#include "string.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#ifdef CONFIG_MB_MDNS_IP_RESOLVER
#include "mdns.h"
#endif
#include "protocol_examples_common.h"

#include "modbus_params.h"  // modbus参数结构
#include "mbcontroller.h"
#include "sdkconfig.h"

#define MB_TCP_PORT                     (CONFIG_FMB_TCP_PORT_DEFAULT)   // TCP端口号示例

// 在特定控制过程中要使用的参数数
#define MASTER_MAX_CIDS num_device_parameters

// 从slave读取参数的次数
#define MASTER_MAX_RETRY                (30)

// Modbus更新cid超时时间
#define UPDATE_CIDS_TIMEOUT_MS          (500)
#define UPDATE_CIDS_TIMEOUT_TICS        (UPDATE_CIDS_TIMEOUT_MS / portTICK_RATE_MS)

// 投票调查之间的超时
#define POLL_TIMEOUT_MS                 (1)
#define POLL_TIMEOUT_TICS               (POLL_TIMEOUT_MS / portTICK_RATE_MS)
#define MB_MDNS_PORT                    (502)

#define MASTER_TAG "MASTER_TEST"

#define MASTER_CHECK(a, ret_val, str, ...) \
    if (!(a)) { \
        ESP_LOGE(MASTER_TAG, "%s(%u): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        return (ret_val); \
    }

// 在适当的结构中获取参数偏移量的宏
#define HOLD_OFFSET(field) ((uint16_t)(offsetof(holding_reg_params_t, field) + 1))
#define INPUT_OFFSET(field) ((uint16_t)(offsetof(input_reg_params_t, field) + 1))
#define COIL_OFFSET(field) ((uint16_t)(offsetof(coil_reg_params_t, field) + 1))
#define DISCR_OFFSET(field) ((uint16_t)(offsetof(discrete_reg_params_t, field) + 1))
#define STR(fieldname) ((const char*)( fieldname ))

// 选项可以用作位掩码或参数限制
#define OPTS(min_val, max_val, step_val) { .opt1 = min_val, .opt2 = max_val, .opt3 = step_val }

#define MB_ID_BYTE0(id) ((uint8_t)(id))
#define MB_ID_BYTE1(id) ((uint8_t)(((uint16_t)(id) >> 8) & 0xFF))
#define MB_ID_BYTE2(id) ((uint8_t)(((uint32_t)(id) >> 16) & 0xFF))
#define MB_ID_BYTE3(id) ((uint8_t)(((uint32_t)(id) >> 24) & 0xFF))

#define MB_ID2STR(id) MB_ID_BYTE0(id), MB_ID_BYTE1(id), MB_ID_BYTE2(id), MB_ID_BYTE3(id)

#if CONFIG_FMB_CONTROLLER_SLAVE_ID_SUPPORT
#define MB_DEVICE_ID (uint32_t)CONFIG_FMB_CONTROLLER_SLAVE_ID
#else
#define MB_DEVICE_ID (uint32_t)0x00112233
#endif

#define MB_MDNS_INSTANCE(pref) pref"mb_master_tcp"

// 主设备访问的modbus设备地址的枚举
// 表中的每个地址是mb_communication_info_t::tcp_ip_addr表中TCP从ip地址的索引
enum {
    MB_DEVICE_ADDR1 = 1, // 从地址1
    MB_DEVICE_COUNT
};

// 枚举设备上所有支持的cid(在参数定义表中使用)
enum {
    CID_INP_DATA_0 = 0,
    CID_HOLD_DATA_0,
    CID_INP_DATA_1,
    CID_HOLD_DATA_1,
    CID_INP_DATA_2,
    CID_HOLD_DATA_2,
    CID_RELAY_P1,
    CID_RELAY_P2,
    CID_COUNT
};

/*
 * Modbus参数字典
 * 表中CID字段必须唯一。
 * Modbus Slave Addr field定义设备对应参数的从地址。
 * Modbus寄存器类型- Modbus寄存器区域的类型(持有寄存器，输入寄存器等)。
 * Reg Start field定义Modbus的起始寄存器号，Reg Size定义了相应的特征的寄存器数。
 * 实例偏移量在相应的形参结构中定义了偏移量，它将被用作保存形参值的实例。
 * 数据类型，数据大小指定特征的类型及其数据大小。
 * Parameter Options字段可用于处理参数值(限制或掩码)的选项。
 * 访问模式-可用于实现自定义选项处理特征(读/写限制，工厂模式值等)。
*/
const mb_parameter_descriptor_t device_parameters[] = {
    // { CID, Param Name, Units, Modbus Slave Addr, Modbus Reg Type, Reg Start, Reg Size, Instance Offset, Data Type, Data Size, Parameter Options, Access Mode}
    { CID_INP_DATA_0, STR("Data_channel_0"), STR("Volts"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 0, 2,
                    INPUT_OFFSET(input_data0), PARAM_TYPE_FLOAT, 4, OPTS( -10, 10, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { CID_HOLD_DATA_0, STR("Humidity_1"), STR("%rH"), MB_DEVICE_ADDR1, MB_PARAM_HOLDING, 0, 2,
            HOLD_OFFSET(holding_data0), PARAM_TYPE_FLOAT, 4, OPTS( 0, 100, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { CID_INP_DATA_1, STR("Temperature_1"), STR("C"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 2, 2,
            INPUT_OFFSET(input_data1), PARAM_TYPE_FLOAT, 4, OPTS( -40, 100, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { CID_HOLD_DATA_1, STR("Humidity_2"), STR("%rH"), MB_DEVICE_ADDR1, MB_PARAM_HOLDING, 2, 2,
            HOLD_OFFSET(holding_data1), PARAM_TYPE_FLOAT, 4, OPTS( 0, 100, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { CID_INP_DATA_2, STR("Temperature_2"), STR("C"), MB_DEVICE_ADDR1, MB_PARAM_INPUT, 4, 2,
            INPUT_OFFSET(input_data2), PARAM_TYPE_FLOAT, 4, OPTS( -40, 100, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { CID_HOLD_DATA_2, STR("Humidity_3"), STR("%rH"), MB_DEVICE_ADDR1, MB_PARAM_HOLDING, 4, 2,
            HOLD_OFFSET(holding_data2), PARAM_TYPE_FLOAT, 4, OPTS( 0, 100, 1 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { CID_RELAY_P1, STR("RelayP1"), STR("on/off"), MB_DEVICE_ADDR1, MB_PARAM_COIL, 0, 8,
            COIL_OFFSET(coils_port0), PARAM_TYPE_U16, 2, OPTS( BIT1, 0, 0 ), PAR_PERMS_READ_WRITE_TRIGGER },
    { CID_RELAY_P2, STR("RelayP2"), STR("on/off"), MB_DEVICE_ADDR1, MB_PARAM_COIL, 8, 8,
            COIL_OFFSET(coils_port1), PARAM_TYPE_U16, 2, OPTS( BIT0, 0, 0 ), PAR_PERMS_READ_WRITE_TRIGGER }
};

// 计算表中的参数个数
const uint16_t num_device_parameters = (sizeof(device_parameters)/sizeof(device_parameters[0]));

// 该表表示从IP地址，对应于device_parameters结构中从服务器的短地址字段
// Modbus TCP栈应该使用这些地址来连接和从slave读取参数
char* slave_ip_address_table[MB_DEVICE_COUNT] = {
#if CONFIG_MB_SLAVE_IP_FROM_STDIN
    "FROM_STDIN",     // Address corresponds to MB_DEVICE_ADDR1 and set to predefined value by user
    NULL
#elif CONFIG_MB_MDNS_IP_RESOLVER
    NULL,
    NULL
#endif
};

#if CONFIG_MB_SLAVE_IP_FROM_STDIN

// Scan IP address according to IPV settings
char* master_scan_addr(int* index, char* buffer)
{
    char* ip_str = NULL;
    unsigned int a[8] = {0};
    int buf_cnt = 0;
#if !CONFIG_EXAMPLE_CONNECT_IPV6
    buf_cnt = sscanf(buffer, "IP%d="IPSTR, index, &a[0], &a[1], &a[2], &a[3]);
    if (buf_cnt == 5) {
        if (-1 == asprintf(&ip_str, IPSTR, a[0], a[1], a[2], a[3])) {
            abort();
        }
    }
#else
    buf_cnt = sscanf(buffer, "IP%d="IPV6STR, index, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7]);
    if (buf_cnt == 9) {
        if (-1 == asprintf(&ip_str, IPV6STR, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7])) {
            abort();
        }
    }
#endif
    return ip_str;
}

static int master_get_slave_ip_stdin(char** addr_table)
{
    char buf[128];
    int index;
    char* ip_str = NULL;
    int buf_cnt = 0;
    int ip_cnt = 0;

    if (!addr_table) {
        return 0;
    }

    ESP_ERROR_CHECK(example_configure_stdin_stdout());
    while(1) {
        if (addr_table[ip_cnt] && strcmp(addr_table[ip_cnt], "FROM_STDIN") == 0) {
            printf("Waiting IP%d from stdin:\r\n", ip_cnt);
            while (fgets(buf, sizeof(buf), stdin) == NULL) {
                fputs(buf, stdout);
            }
            buf_cnt = strlen(buf);
            buf[buf_cnt - 1] = '\0';
            fputc('\n', stdout);
            ip_str = master_scan_addr(&index, buf);
            if (ip_str != NULL) {
                ESP_LOGI(MASTER_TAG, "IP(%d) = [%s] set from stdin.", ip_cnt, ip_str);
                if ((ip_cnt >= MB_DEVICE_COUNT) || (index != ip_cnt)) {
                    addr_table[ip_cnt] = NULL;
                    break;
                }
                addr_table[ip_cnt++] = ip_str;
            } else {
                // End of configuration
                addr_table[ip_cnt++] = NULL;
                break;
            }
        } else {
            if (addr_table[ip_cnt]) {
                ESP_LOGI(MASTER_TAG, "Leave IP(%d) = [%s] set manually.", ip_cnt, addr_table[ip_cnt]);
                ip_cnt++;
            } else {
                ESP_LOGI(MASTER_TAG, "IP(%d) is not set in the table.", ip_cnt);
                break;
            }
        }
    }
    return ip_cnt;
}

#elif CONFIG_MB_MDNS_IP_RESOLVER

// convert MAC from binary format to string
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

static void master_start_mdns_service()
{
    char temp_str[32] = {0};
    uint8_t sta_mac[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(sta_mac, ESP_MAC_WIFI_STA));
    char* hostname = gen_mac_str(sta_mac, MB_MDNS_INSTANCE("")"_", temp_str);
    // initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    // set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_LOGI(MASTER_TAG, "mdns hostname set to: [%s]", hostname);

    // set default mDNS instance name
    ESP_ERROR_CHECK(mdns_instance_name_set(MB_MDNS_INSTANCE("esp32_")));

    // structure with TXT records
    mdns_txt_item_t serviceTxtData[] = {
        {"board","esp32"}
    };

    // initialize service
    ESP_ERROR_CHECK(mdns_service_add(MB_MDNS_INSTANCE(""), "_modbus", "_tcp", MB_MDNS_PORT, serviceTxtData, 1));
    // add mac key string text item
    ESP_ERROR_CHECK(mdns_service_txt_item_set("_modbus", "_tcp", "mac", gen_mac_str(sta_mac, "\0", temp_str)));
    // add slave id key txt item
    ESP_ERROR_CHECK( mdns_service_txt_item_set("_modbus", "_tcp", "mb_id", gen_id_str("\0", temp_str)));
}

static char* master_get_slave_ip_str(mdns_ip_addr_t* address, mb_tcp_addr_type_t addr_type)
{
    mdns_ip_addr_t* a = address;
    char* slave_ip_str = NULL;

    while (a) {
        if ((a->addr.type == IPADDR_TYPE_V6) && (addr_type == MB_IPV6)) {
            if (-1 == asprintf(&slave_ip_str, IPV6STR, IPV62STR(a->addr.u_addr.ip6))) {
                abort();
            }
        } else if ((a->addr.type == IPADDR_TYPE_V4) && (addr_type == MB_IPV4)) {
            if (-1 == asprintf(&slave_ip_str, IPSTR, IP2STR(&(a->addr.u_addr.ip4)))) {
                abort();
            }
        }
        if (slave_ip_str) {
            break;
        }
        a = a->next;
    }
    return slave_ip_str;
}

static esp_err_t master_resolve_slave(const char* name, mdns_result_t* result, char** resolved_ip, mb_tcp_addr_type_t addr_type)
{
    if (!name || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    mdns_result_t* r = result;
    int t;
    char* slave_ip = NULL;
    for (; r ; r = r->next) {
        if ((r->ip_protocol == MDNS_IP_PROTOCOL_V4) && (addr_type == MB_IPV6)) {
            continue;
        } else if ((r->ip_protocol == MDNS_IP_PROTOCOL_V6) && (addr_type == MB_IPV4)) {
            continue;
        }
        // Check host name for Modbus short address and
        // append it into slave ip address table
        if ((strcmp(r->instance_name, name) == 0) && (r->port == CONFIG_FMB_TCP_PORT_DEFAULT)) {
            printf("  PTR : %s\n", r->instance_name);
            if (r->txt_count) {
                printf("  TXT : [%u] ", r->txt_count);
                for ( t = 0; t < r->txt_count; t++) {
                    printf("%s=%s; ", r->txt[t].key, r->txt[t].value?r->txt[t].value:"NULL");
                }
                printf("\n");
            }
            slave_ip = master_get_slave_ip_str(r->addr, addr_type);
            if (slave_ip) {
                ESP_LOGI(MASTER_TAG, "Resolved slave %s[%s]:%u", r->hostname, slave_ip, r->port);
                *resolved_ip = slave_ip;
                return ESP_OK;
            }
        }
    }
    *resolved_ip = NULL;
    ESP_LOGD(MASTER_TAG, "Fail to resolve slave: %s", name);
    return ESP_ERR_NOT_FOUND;
}

static int master_create_slave_list(mdns_result_t* results, char** addr_table, mb_tcp_addr_type_t addr_type)
{
    if (!results) {
        return -1;
    }
    int i, addr, resolved = 0;
    const mb_parameter_descriptor_t* pdescr = &device_parameters[0];
    char** ip_table = addr_table;
    char slave_name[22] = {0};
    char* slave_ip = NULL;

    for (i = 0; (i < num_device_parameters && pdescr); i++, pdescr++) {
        addr = pdescr->mb_slave_addr;
        if (-1 == sprintf(slave_name, "mb_slave_tcp_%02X", addr)) {
            ESP_LOGI(MASTER_TAG, "Fail to create instance name for index: %d", addr);
            abort();
        }
        if (!ip_table[addr - 1]) {
            esp_err_t err = master_resolve_slave(slave_name, results, &slave_ip, addr_type);
            if (err != ESP_OK) {
                ESP_LOGE(MASTER_TAG, "Index: %d, sl_addr: %d, name:%s, failed to resolve!",
                                        i, addr, slave_name);
                // Set correspond index to NULL indicate host not resolved
                ip_table[addr - 1] = NULL;
                continue;
            }
            ip_table[addr - 1] = slave_ip; //slave_name;
            ESP_LOGI(MASTER_TAG, "Index: %d, sl_addr: %d, name:%s, resolve to IP: [%s]",
                                    i, addr, slave_name, slave_ip);
            resolved++;
        } else {
            ESP_LOGI(MASTER_TAG, "Index: %d, sl_addr: %d, name:%s, set to IP: [%s]",
                                    i, addr, slave_name, ip_table[addr - 1]);
            resolved++;
        }
    }
    return resolved;
}

static void master_destroy_slave_list(char** table)
{
    for (int i = 0; ((i < MB_DEVICE_COUNT) && table[i] != NULL); i++) {
        if (table[i]) {
            free(table[i]);
        }
    }
}

static int master_query_slave_service(const char * service_name, const char * proto, mb_tcp_addr_type_t addr_type)
{
    ESP_LOGI(MASTER_TAG, "Query PTR: %s.%s.local", service_name, proto);

    mdns_result_t* results = NULL;
    int count = 0;

    esp_err_t err = mdns_query_ptr(service_name, proto, 3000, 20, &results);
    if(err){
        ESP_LOGE(MASTER_TAG, "Query Failed: %s", esp_err_to_name(err));
        return count;
    }
    if(!results){
        ESP_LOGW(MASTER_TAG, "No results found!");
        return count;
    }

    count = master_create_slave_list(results, slave_ip_address_table, addr_type);

    mdns_query_results_free(results);
    return count;
}
#endif

// 根据参数说明表获取参数存储(实例)指针的函数
static void* master_get_param_data(const mb_parameter_descriptor_t* param_descriptor)
{
    assert(param_descriptor != NULL);
    void* instance_ptr = NULL;
    if (param_descriptor->param_offset != 0) 
    {
       switch(param_descriptor->mb_param_type)
       {
           case MB_PARAM_HOLDING:
               instance_ptr = ((void*)&holding_reg_params + param_descriptor->param_offset - 1);
               break;
           case MB_PARAM_INPUT:
               instance_ptr = ((void*)&input_reg_params + param_descriptor->param_offset - 1);
               break;
           case MB_PARAM_COIL:
               instance_ptr = ((void*)&coil_reg_params + param_descriptor->param_offset - 1);
               break;
           case MB_PARAM_DISCRETE:
               instance_ptr = ((void*)&discrete_reg_params + param_descriptor->param_offset - 1);
               break;
           default:
               instance_ptr = NULL;
               break;
       }
    } 
    else {
        ESP_LOGE(MASTER_TAG, "Wrong parameter offset for CID #%d", param_descriptor->cid);
        assert(instance_ptr != NULL);
    }
    return instance_ptr;
}

// 用户操作功能读取从值，查看告警
static void master_operation_func(void *arg)
{
    esp_err_t err = ESP_OK;
    float value = 0;
    bool alarm_state = false;
    const mb_parameter_descriptor_t* param_descriptor = NULL;   // 特征描述符类型用于描述特征，并将其与反映其数据的Modbus参数联系起来

    ESP_LOGI(MASTER_TAG, "Start modbus test...");
    
    for(uint16_t retry = 0; retry <= MASTER_MAX_RETRY && (!alarm_state); retry++) 
    {
        // 从slave(s)读取所有发现的特征
        for (uint16_t cid = 0; (err != ESP_ERR_NOT_FOUND) && cid < MASTER_MAX_CIDS; cid++) 
        {
            // 从参数描述表中获取数据 并使用这些信息填充特征描述表 将所有必需的字段放在一个表中
            err = mbc_master_get_cid_info(cid, &param_descriptor);
            if ((err != ESP_ERR_NOT_FOUND) && (param_descriptor != NULL)) 
            {
                void* temp_data_ptr = master_get_param_data(param_descriptor);
                assert(temp_data_ptr);
                uint8_t type = 0;
                // 从modbus从设备读取参数，名称由name定义，具有cid
                err = mbc_master_get_parameter(cid, (char*)param_descriptor->param_key, (uint8_t*)&value, &type);
                if (err == ESP_OK) 
                {
                    if ((param_descriptor->mb_param_type == MB_PARAM_HOLDING) || (param_descriptor->mb_param_type == MB_PARAM_INPUT)) 
                    {
                        ESP_LOGI(MASTER_TAG, "Characteristic #%d %s (%s) value = %f (0x%x) read successful.",
                                        param_descriptor->cid,
                                        (char*)param_descriptor->param_key, 
                                        (char*)param_descriptor->param_units,
                                        value, 
                                        *(uint32_t*)&value);
                        if (((value > param_descriptor->param_opts.max) || (value < param_descriptor->param_opts.min))) 
                        {
                                alarm_state = true;
                                break;
                        }
                    } 
                    else 
                    {
                        uint16_t state = *(uint16_t*)&value;
                        const char* rw_str = (state & param_descriptor->param_opts.opt1) ? "ON" : "OFF";
                        ESP_LOGI(MASTER_TAG, "Characteristic #%d %s (%s) value = %s (0x%x) read successful.",
                                        param_descriptor->cid,
                                        (char*)param_descriptor->param_key, 
                                        (char*)param_descriptor->param_units,
                                        (const char*)rw_str,
                                        *(uint16_t*)&value);
                        if (state & param_descriptor->param_opts.opt1) 
                        {
                            alarm_state = true;
                            break;
                        }
                    }
                } 
                else 
                {
                    ESP_LOGE(MASTER_TAG, "Characteristic #%d (%s) read fail, err = %d (%s).",
                                        param_descriptor->cid,
                                        (char*)param_descriptor->param_key,
                                        (int)err,
                                        (char*)esp_err_to_name(err));
                }
                vTaskDelay(POLL_TIMEOUT_TICS); // timeout between polls
            }
        }
        vTaskDelay(UPDATE_CIDS_TIMEOUT_TICS);
    }
    
    if (alarm_state) 
    {   
        ESP_LOGI(MASTER_TAG, "Alarm triggered by cid #%d.",
                                        param_descriptor->cid);
    } else 
    {
        ESP_LOGE(MASTER_TAG, "Alarm is not triggered after %d retries.",
                                        MASTER_MAX_RETRY);
    }
    ESP_LOGI(MASTER_TAG, "Destroy master...");
    vTaskDelay(100);
    // 销毁Modbus控制器和堆栈
    ESP_ERROR_CHECK(mbc_master_destroy());
}

// Modbus主机初始化
static esp_err_t master_init(void)
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
    // Start mdns service and register device
    master_start_mdns_service();
#endif

    ESP_ERROR_CHECK(example_connect());                 // 配置Wi-Fi或以太网，连接，等待IP

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));     // 设置当前省电类型

    mb_communication_info_t comm_info = { 0 };
    comm_info.ip_port = MB_TCP_PORT;                    // TCP端口号示例
#if !CONFIG_EXAMPLE_CONNECT_IPV6
    comm_info.ip_addr_type = MB_IPV4;                   // 网络通讯协议地址类型 IPV4
#else
    comm_info.ip_addr_type = MB_IPV6;
#endif
    comm_info.ip_mode = MB_MODE_TCP;                    // TCP通信方式
    comm_info.ip_addr = (void*)slave_ip_address_table;  // Modbus地址表用于连接
    void * nif = NULL;
    ESP_ERROR_CHECK(tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_STA, &nif));   // 获取分配给接口的LwIP netif*
    comm_info.ip_netif_ptr = nif;                       // 网络通讯协议的网络接口

#if CONFIG_MB_MDNS_IP_RESOLVER
    int res = 0;
    for (int retry = 0; (res < num_device_parameters) && (retry < 10); retry++) {
        res = master_query_slave_service("_modbus", "_tcp", comm_info.ip_addr_type);
    }
    if (res < num_device_parameters) {
        ESP_LOGE(MASTER_TAG, "Could not resolve one or more slave IP addresses, resolved: %d out of %d.", res, num_device_parameters );
        ESP_LOGE(MASTER_TAG, "Make sure you configured all slaves according to device parameter table and they alive in the network.");
        return ESP_ERR_NOT_FOUND;
    }
    mdns_free();
#elif CONFIG_MB_SLAVE_IP_FROM_STDIN
    int ip_cnt = master_get_slave_ip_stdin(slave_ip_address_table);
    if (ip_cnt) {
        ESP_LOGI(MASTER_TAG, "Configured %d IP addresse(s).", ip_cnt);
    } else {
        ESP_LOGE(MASTER_TAG, "Fail to get IP address from stdin. Continue.");
    }
#endif

    void* master_handler = NULL;

    // 初始化Modbus TCP主控制器接口
    esp_err_t err = mbc_master_init_tcp(&master_handler);
    MASTER_CHECK((master_handler != NULL), ESP_ERR_INVALID_STATE, "mb controller initialization fail.");
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE, "mb controller initialization fail, returns(0x%x).", (uint32_t)err);

    // 设置控制器的Modbus通信参数
    err = mbc_master_setup((void*)&comm_info);
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE, "mb controller setup fail, returns(0x%x).", (uint32_t)err);

    // 分配Modbus控制器接口参数说明表
    err = mbc_master_set_descriptor(&device_parameters[0], num_device_parameters);
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE, "mb controller set descriptor fail, returns(0x%x).", (uint32_t)err);
    ESP_LOGI(MASTER_TAG, "Modbus master stack initialized...");

    // 启动Modbus通信栈
    err = mbc_master_start();
    MASTER_CHECK((err == ESP_OK), ESP_ERR_INVALID_STATE, "mb controller start fail, returns(0x%x).", (uint32_t)err);
    vTaskDelay(5);
    return err;
}

void app_main(void)
{
    // 初始化设备外设和对象
    ESP_ERROR_CHECK(master_init());
    vTaskDelay(10);
    
    master_operation_func(NULL);
#if CONFIG_MB_MDNS_IP_RESOLVER
    master_destroy_slave_list(slave_ip_address_table);
#endif

}
