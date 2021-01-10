#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "mdns.h"
#include "driver/gpio.h"
#include <sys/socket.h>
#include <netdb.h>


#define EXAMPLE_MDNS_INSTANCE   CONFIG_MDNS_INSTANCE
#define EXAMPLE_BUTTON_GPIO     0

static const char *TAG = "mdns-test";
static char* generate_hostname();

static void initialise_mdns(void)
{
    // 基于sdkconfig生成主机名，可以向其中添加MAC地址的一部分。
    char* hostname = generate_hostname();
    // 初始化 mdns
    ESP_ERROR_CHECK( mdns_init() );
    // 设置mDNS主机名(如果要发布服务，需要设置)
    ESP_ERROR_CHECK( mdns_hostname_set(hostname) );
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);
    // 设置默认的mDNS实例名称
    ESP_ERROR_CHECK( mdns_instance_name_set(EXAMPLE_MDNS_INSTANCE) );

    // 带有TXT记录的结构
    mdns_txt_item_t serviceTxtData[3] = {
        {"board","esp32"},
        {"u","user"},
        {"p","password"}
    };

    // 初始化服务
    ESP_ERROR_CHECK( mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, serviceTxtData, 3) );
    // 添加另一个TXT项
    ESP_ERROR_CHECK( mdns_service_txt_item_set("_http", "_tcp", "path", "/foobar") );
    // 更改TXT项值
    ESP_ERROR_CHECK( mdns_service_txt_item_set("_http", "_tcp", "u", "admin") );
    free(hostname);
}

/* 这些字符串匹配tcpip_adapter_if_t枚举 */
static const char * if_str[] = {"STA", "AP", "ETH", "MAX"};

/* 这些字符串匹配mdns_ip_protocol_t枚举 */
static const char * ip_protocol_str[] = {"V4", "V6", "MAX"};

static void mdns_print_results(mdns_result_t * results){
    mdns_result_t * r = results;
    mdns_ip_addr_t * a = NULL;
    int i = 1, t;
    while(r)
    {
        printf("%d: Interface: %s, Type: %s\n", i++, if_str[r->tcpip_if], ip_protocol_str[r->ip_protocol]);
        if(r->instance_name)
        {
            printf("  PTR : %s\n", r->instance_name);
        }
        if(r->hostname)
        {
            printf("  SRV : %s.local:%u\n", r->hostname, r->port);
        }
        if(r->txt_count)
        {
            printf("  TXT : [%u] ", r->txt_count);
            for(t=0; t<r->txt_count; t++)
            {
                printf("%s=%s; ", r->txt[t].key, r->txt[t].value?r->txt[t].value:"NULL");
            }
            printf("\n");
        }
        a = r->addr;
        while(a)
        {
            if(a->addr.type == IPADDR_TYPE_V6)
            {
                printf("  AAAA: " IPV6STR "\n", IPV62STR(a->addr.u_addr.ip6));
            } else 
            {
                printf("  A   : " IPSTR "\n", IP2STR(&(a->addr.u_addr.ip4)));
            }
            a = a->next;
        }
        r = r->next;
    }
}

static void query_mdns_service(const char * service_name, const char * proto)
{
    ESP_LOGI(TAG, "Query PTR: %s.%s.local", service_name, proto);

    mdns_result_t * results = NULL;
    esp_err_t err = mdns_query_ptr(service_name, proto, 3000, 20,  &results);
    if(err)
    {
        ESP_LOGE(TAG, "Query Failed: %s", esp_err_to_name(err));
        return;
    }
    if(!results)
    {
        ESP_LOGW(TAG, "No results found!");
        return;
    }

    mdns_print_results(results);
    mdns_query_results_free(results);
}

static void query_mdns_host(const char * host_name)
{
    ESP_LOGI(TAG, "Query A: %s.local", host_name);

    struct ip4_addr addr;
    addr.addr = 0;

    esp_err_t err = mdns_query_a(host_name, 2000,  &addr);
    if(err)
    {
        if(err == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGW(TAG, "%s: Host was not found!", esp_err_to_name(err));
            return;
        }
        ESP_LOGE(TAG, "Query Failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Query A: %s.local resolved to: " IPSTR, host_name, IP2STR(&addr));
}

static void initialise_button(void)
{
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;  // 禁止中断
    io_conf.pin_bit_mask = 1;               // 引脚
    io_conf.mode = GPIO_MODE_INPUT;         // 输入模式
    io_conf.pull_up_en = 1;                 // 上拉
    io_conf.pull_down_en = 0;               // 禁止下拉
    gpio_config(&io_conf);
}

static void check_button(void)
{
    static bool old_level = true;
    // GPIO获取输入电平
    bool new_level = gpio_get_level(EXAMPLE_BUTTON_GPIO);
    if (!new_level && old_level) 
    {
        query_mdns_host("esp32");
        query_mdns_service("_arduino", "_tcp");
        query_mdns_service("_http", "_tcp");
        query_mdns_service("_printer", "_tcp");
        query_mdns_service("_ipp", "_tcp");
        query_mdns_service("_afpovertcp", "_tcp");
        query_mdns_service("_smb", "_tcp");
        query_mdns_service("_ftp", "_tcp");
        query_mdns_service("_nfs", "_tcp");
    }
    old_level = new_level;
}

static void mdns_example_task(void *pvParameters)
{
#if CONFIG_MDNS_RESOLVE_TEST_SERVICES == 1
    /* Send initial queries that are started by CI tester */
    query_mdns_host("tinytester");
#endif

    while(1) 
    {
        check_button();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());                  // 初始化默认的NVS分区 
    ESP_ERROR_CHECK(esp_netif_init());                  // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环

    initialise_mdns();

    ESP_ERROR_CHECK(example_connect());                 // 配置Wi-Fi或以太网，连接，等待IP

    initialise_button();
    xTaskCreate(&mdns_example_task, "mdns_example_task", 2048*5, NULL, 5, NULL);
}

/** Generate host name based on sdkconfig, optionally adding a portion of MAC address to it.
 *  @return host name string allocated from the heap
 */
static char* generate_hostname()
{
#ifndef CONFIG_MDNS_ADD_MAC_TO_HOSTNAME
    return strdup(CONFIG_MDNS_HOSTNAME);
#else
    uint8_t mac[6];
    char   *hostname;
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (-1 == asprintf(&hostname, "%s-%02X%02X%02X", CONFIG_MDNS_HOSTNAME, mac[3], mac[4], mac[5])) {
        abort();
    }
    return hostname;
#endif
}
