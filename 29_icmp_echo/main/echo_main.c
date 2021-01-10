#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_console.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "ping/ping_sock.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

static void cmd_ping_on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    // 获取ping会话的运行时配置文件
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    printf("%d bytes from %s icmp_seq=%d ttl=%d time=%d ms\n", recv_len, ipaddr_ntoa((ip_addr_t*)&target_addr), seqno, ttl, elapsed_time);
}

static void cmd_ping_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    printf("From %s icmp_seq=%d timeout\n",ipaddr_ntoa((ip_addr_t*)&target_addr), seqno);
}

static void cmd_ping_on_ping_end(esp_ping_handle_t hdl, void *args)
{
    ip_addr_t target_addr;
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    uint32_t loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
    if (IP_IS_V4(&target_addr)) {
        printf("\n--- %s ping statistics ---\n", inet_ntoa(*ip_2_ip4(&target_addr)));
    }
#if LWIP_IPV6
    else {
        printf("\n--- %s ping statistics ---\n", inet6_ntoa(*ip_2_ip6(&target_addr)));
    }
#endif
    printf("%d packets transmitted, %d received, %d%% packet loss, time %dms\n", transmitted, received, loss, total_time_ms);
    // 删除ping会话，这样我们就可以清理所有的资源并创建一个新的ping会话
    // 我们不需要在回调函数中调用delete函数，相反，我们可以在其他任务中调用delete函数
    esp_ping_delete_session(hdl);
}

static struct {
    struct arg_dbl *timeout;
    struct arg_dbl *interval;
    struct arg_int *data_size;
    struct arg_int *count;
    struct arg_int *tos;
    struct arg_int *interface;
    struct arg_str *host;
    struct arg_end *end;
} ping_args;

static int do_ping_cmd(int argc, char **argv)
{
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();

    int nerrors = arg_parse(argc, argv, (void **)&ping_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ping_args.end, argv[0]);
        return 1;
    }

    if (ping_args.timeout->count > 0) {
        config.timeout_ms = (uint32_t)(ping_args.timeout->dval[0] * 1000);
    }

    if (ping_args.interval->count > 0) {
        config.interval_ms = (uint32_t)(ping_args.interval->dval[0] * 1000);
    }

    if (ping_args.data_size->count > 0) {
        config.data_size = (uint32_t)(ping_args.data_size->ival[0]);
    }

    if (ping_args.count->count > 0) {
        config.count = (uint32_t)(ping_args.count->ival[0]);
    }

    if (ping_args.tos->count > 0) {
        config.tos = (uint32_t)(ping_args.tos->ival[0]);
    }

    if (ping_args.interface->count > 0) {
        config.interface = (uint32_t)(ping_args.interface->ival[0]);
    }

    // 解析IP地址
    ip_addr_t target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
#if LWIP_IPV6
    struct sockaddr_in6 sock_addr6;
    if (inet_pton(AF_INET6, ping_args.host->sval[0], &sock_addr6.sin6_addr) == 1) {
        // 转换ip6字符串为ip6地址
        ipaddr_aton(ping_args.host->sval[0], &target_addr);
    } else
#endif
    {
        struct addrinfo hint;
        struct addrinfo *res = NULL;
        memset(&hint, 0, sizeof(hint));
        // 将ip4字符串或主机名转换为ip4或ip6地址
        if (getaddrinfo(ping_args.host->sval[0], NULL, &hint, &res) != 0) {
            printf("ping: unknown host %s\n", ping_args.host->sval[0]);
            return 1;
        }
        if (res->ai_family == AF_INET) {
            struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
            inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
        }
#if LWIP_IPV6
        else {
            struct in6_addr addr6 = ((struct sockaddr_in6 *) (res->ai_addr))->sin6_addr;
            inet6_addr_to_ip6addr(ip_2_ip6(&target_addr), &addr6);
        }
#endif
        freeaddrinfo(res);
    }
    config.target_addr = target_addr;

    // 设置回调函数
    esp_ping_callbacks_t cbs = {
        .on_ping_success = cmd_ping_on_ping_success,    // 内部ping线程收到ICMP echo - reply报文时调用
        .on_ping_timeout = cmd_ping_on_ping_timeout,    // 内部ping线程在收到ICMP echo - reply报文超时时调用
        .on_ping_end = cmd_ping_on_ping_end,            // 当一个ping会话完成时，由内部ping线程调用
        .cb_args = NULL                                 // 回调函数的参数
    };
    esp_ping_handle_t ping;
    // 创建ping会话
    esp_ping_new_session(&config, &cbs, &ping);
    // 启动ping会话
    esp_ping_start(ping);

    return 0;
}

static void register_ping(void)
{
    ping_args.timeout = arg_dbl0("W", "timeout", "<t>", "Time to wait for a response, in seconds");
    ping_args.interval = arg_dbl0("i", "interval", "<t>", "Wait interval seconds between sending each packet");
    ping_args.data_size = arg_int0("s", "size", "<n>", "Specify the number of data bytes to be sent");
    ping_args.count = arg_int0("c", "count", "<n>", "Stop after sending count packets");
    ping_args.tos = arg_int0("Q", "tos", "<n>", "Set Type of Service related bits in IP datagrams");
    ping_args.interface = arg_int0("I", "interface", "<n>", "Set Interface number to send out the packet");
    ping_args.host = arg_str1(NULL, NULL, "<host>", "Host address");
    ping_args.end = arg_end(1);
    const esp_console_cmd_t ping_cmd = {
        .command = "ping",                                  // 命令名称
        .help = "send ICMP ECHO_REQUEST to network hosts",  // 命令的帮助文本，由“帮助命令”显示
        .hint = NULL,                                       // 提示文本，通常列出可能的参数。
        .func = &do_ping_cmd,                               // 执行命令的函数
        .argtable = &ping_args
    };
    // 注册控制台命令
    ESP_ERROR_CHECK(esp_console_cmd_register(&ping_cmd));
}

// 处理'quit'命令
static int do_cmd_quit(int argc, char **argv)
{
    printf("ByeBye\r\n");
    return 0;
}

// 注册命令 quit
static esp_err_t register_quit(void)
{
    esp_console_cmd_t command = {
        .command = "quit",
        .help = "Quit REPL environment",
        .func = &do_cmd_quit
    };
    // 注册控制台命令
    return esp_console_cmd_register(&command);
}

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
    // 在使用其他控制台模块特性之前，调用此函数一次
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
    ESP_ERROR_CHECK(nvs_flash_init());                  // 初始化默认的NVS分区 
    ESP_ERROR_CHECK(esp_netif_init());                  // 初始化tcpip适配器
    ESP_ERROR_CHECK(esp_event_loop_create_default());   // 创建默认事件循环

    ESP_ERROR_CHECK(example_connect());                 // 配置Wi-Fi或以太网，连接，等待IP

    initialize_console();
    // 注册命令 ping
    register_ping();
    // 注册命令 quit
    register_quit();

    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    const char *prompt = LOG_COLOR_I "esp8266> " LOG_RESET_COLOR;

    printf("\n ==========================================================================\n");
    printf(" |               PING help command                                         |\n");
    printf(" |      send ICMP ECHO_REQUEST to network hosts                            |\n");
    printf(" |                                                                         |\n");
    printf(" |  ping  [-W <t>] [-i <t>] [-s <n>] [-c <n>] [-Q <n>] <host>              |\n");
    printf(" |  -W, --timeout=<t>  Time to wait for a response, in seconds             |\n");
    printf(" |  -i, --interval=<t>  Wait interval seconds between sending each packet  |\n");
    printf(" |  -s, --size=<n>  Specify the number of data bytes to be sent            |\n");
    printf(" |  -c, --count=<n>  Stop after sending count packets                      |\n");
    printf(" |  -Q, --tos=<n>  Set Type of Service related bits in IP datagrams        |\n");
    printf(" |  -I, --interface=<n>  Set Interface number to send out the packet       |\n");
    printf(" |   <host>  Host address or IP address                                    |\n");
    printf(" |                                                                         |\n");
    printf(" ===========================================================================\n\n");

    // 判断终端是否支持转义序列
    int probe_status = linenoiseProbe();
    /* 0 表示成功 */
    if (probe_status)   
    {
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead.\n");
        linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
        /* Since the terminal doesn't support escape sequences,
         * don't use color codes in the prompt.
         */
        prompt = "esp8266> ";
#endif //CONFIG_LOG_COLORS
    }

    /* Main loop */
    while (true) 
    {
        // 使用linenoise获取一条线， 回车返回
        char *line = linenoise(prompt);
        // 忽略空行
        if (line == NULL)
        { 
            continue;
        }
        // 将命令添加到历史记录中
        linenoiseHistoryAdd(line);

        // 尝试运行该命令
        int ret;
        // 运行命令行
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
