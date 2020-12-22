#ifndef __UDP_XUHONG_H__
#define __UDP_XUHONG_H__


extern EventGroupHandle_t           udp_event_group; 
#define WIFI_CONNECTED_BIT      	BIT0                    //通过IP连接到AP
#define UDP_CONNCETED_SUCCESS   	BIT1                    //在最大重试次数后，未能连接



void wifi_init_softap(void);                    //wifi的softap初始化
esp_err_t create_udp_server();                  //创建udp服务器套接字
void send_recv_data(void *pvParameters);        //发送/接收数据
int get_socket_error_code(int socket);          //获取套接字错误代码
int show_socket_error_reason(int socket);       //显示套接字错误代码
int check_connected_socket(void);               //检查连接的套接字
void close_socket(void);                        //关闭套接字


#endif

