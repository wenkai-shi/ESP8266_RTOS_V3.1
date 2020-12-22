#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_spi_flash.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/pwm.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "led.h"
#include "usart.h"
#include "key.h"
#include "dht11.h"
#include "delay.h"
#include "oled.h"
#include "i2c_master.h"
#include "intr.h"

static const char *TAG = "MAIN";

/* 初始任务 */
#define START_TASK_PRIO			5		//任务优先级
#define START_STK_SIZE 			2048  	//任务堆栈大小	
TaskHandle_t StartTask_Handler;			//任务句柄
void start_task(void *pvParameters);	//任务函数

/* KEY任务 */
#define KEY_TASK_PRIO		4		    //任务优先级
#define KEY_STK_SIZE 		1024  	    //任务堆栈大小	
TaskHandle_t KEYTask_Handler;		    //任务句柄 
void key_task(void *p_arg);		        //任务函数

/* DHT11任务 */
#define DHT11_TASK_PRIO		3		//任务优先级
#define DHT11_STK_SIZE 		1024  	//任务堆栈大小	
TaskHandle_t DHT11Task_Handler;		//任务句柄 
void dht11_task(void *p_arg);		//任务函数

/* PWM任务 */
#define PWM_TASK_PRIO		2		//任务优先级
#define PWM_STK_SIZE 		1024  	//任务堆栈大小	
TaskHandle_t PWMTask_Handler;		//任务句柄 
void pwm_task(void *p_arg);			//任务函数

/* NVS任务 */
#define NVS_TASK_PRIO		1		//任务优先级
#define NVS_STK_SIZE 		1024  	//任务堆栈大小	
static void Task_NVS_Write(void *pvParameters);

//自定义一个结构体
typedef struct
{
    char name[10];
    int8_t age;
    bool sex;
} User_Info;

static const char *TB_SELF = "Tb_Self";                 //数据库的表名
static const char *FILED_SELF_i8 = "int8_t_Self";       //保存与读取 int8_t 类型的 字段名
static const char *FILED_SELF_Str = "str_Self";         //保存与读取 字符串类型的  字段名
static const char *FILED_SELF_Group = "group_Self";     //保存与读取 数组类型的    字段名
static const char *FILED_SELF_Struct = "struct_Self";   //保存与读取 结构体类型的  字段名

void app_main()
{
    LED_Init();             //LED初始化
    KEY_Init();             //key
	Uart_Init();			//UART
	Intr_Init();			//中断
	OLED_Init();			//OLED
	DHT11_init();			//DHT11

    printf("hello world\n");
	printf("SDK version:%s\n", esp_get_idf_version());
	OLED_ShowString(0,0,(uint8_t *)"hello world");
	//创建开始任务
	xTaskCreate((TaskFunction_t) start_task,		//任务函数	
				(const char*   ) "start_task",		//任务名称
				(uint16_t      ) START_STK_SIZE,	//任务堆栈大小
				(void *        ) NULL,				//传递给任务函数的参数
				(UBaseType_t   ) START_TASK_PRIO,	//任务优先级
				(TaskHandle_t *) StartTask_Handler);//任务句柄  
}

/* 开始任务任务函数 */
void start_task(void *pvParameters)
{
	taskENTER_CRITICAL(); 		//进入临界区
	//创建KEY任务
	xTaskCreate((TaskFunction_t) key_task,		    //任务函数	
				(const char*   ) "key_task",		//任务名称
				(uint16_t      ) KEY_STK_SIZE,	    //任务堆栈大小
				(void *        ) NULL,				//传递给任务函数的参数
				(UBaseType_t   ) KEY_TASK_PRIO,	    //任务优先级
				(TaskHandle_t *) KEYTask_Handler);  //任务句柄 

	//创建DHT11任务
	xTaskCreate((TaskFunction_t) dht11_task,		//任务函数	
				(const char*   ) "dht11_task",		//任务名称
				(uint16_t      ) DHT11_STK_SIZE,	//任务堆栈大小
				(void *        ) NULL,				//传递给任务函数的参数
				(UBaseType_t   ) DHT11_TASK_PRIO,	//任务优先级
				(TaskHandle_t *) DHT11Task_Handler);//任务句柄  

	//创建PWM任务
	xTaskCreate((TaskFunction_t) pwm_task,			//任务函数	
				(const char*   ) "pwm_task",		//任务名称
				(uint16_t      ) PWM_STK_SIZE,		//任务堆栈大小
				(void *        ) NULL,				//传递给任务函数的参数
				(UBaseType_t   ) PWM_TASK_PRIO,		//任务优先级
				(TaskHandle_t *) PWMTask_Handler);	//任务句柄  

	//创建NVS任务
	xTaskCreate((TaskFunction_t) Task_NVS_Write,	//任务函数	
				(const char*   ) "Task_NVS_Write",	//任务名称
				(uint16_t      ) NVS_STK_SIZE,		//任务堆栈大小
				(void *        ) NULL,				//传递给任务函数的参数
				(UBaseType_t   ) NVS_TASK_PRIO,		//任务优先级
				(TaskHandle_t *) NULL);				//任务句柄 

	vTaskDelete(StartTask_Handler);	//删除开始任务

	taskEXIT_CRITICAL(); 			//退出临界区
}

/* 任务KEY函数 */
void key_task(void *p_arg)
{
    static int key_up = 1;   /* 按键松开标志 */
    while(1)
    {
        /* 检测按键是否按下 */
        if(key_up && (gpio_get_level(KEY_DATA_PIN)) == 0)
        {
            vTaskDelay(20 / portTICK_RATE_MS);      //消抖
            key_up = 0;
            if (gpio_get_level(KEY_DATA_PIN) == 0)
            {
                printf("BOOT Key pressed!\n");      /* 按键BOOT按下，按键按下处理*/
            }
        }
        else if (gpio_get_level(KEY_DATA_PIN) == 1)
        {
            key_up = 1;     /* 按键已松开 */
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

/* 任务DHT11函数 */
void dht11_task(void *p_arg)
{
	bool led_flag = 0;
	int tem, hum;
    int ret;
    while(1)
    {
		led_flag = !led_flag;
		//gpio_set_level(GPIO_NUM_4, led_flag);
        ret = get_dth_data(&tem, &hum);
        if(!ret)
        {
            printf("Tem = %d.%dC   Hum = %d%%\n",tem/10,abs(tem%10), hum/10);     
        }
        vTaskDelay(2000 / portTICK_RATE_MS);
    }
}

/* 任务PWM函数 */
void pwm_task(void *p_arg)
{
	uint32_t i = 0;
	uint32_t duties = 1000;          	//PWM占空比,一个周期内的通电时长
	const uint32_t pin_num[1] = {4}; 	//指定PWM的输出IO口通道，
	float phase[1] = {0};          		//当多个PWM通道时的相位

	pwm_init(1000, &duties, 1, pin_num);	//设置周期、占空比、通道数、通道的GPIO
    pwm_set_phases(phase);					//设置相位：使输出达到最大功率
    pwm_set_channel_invert(1ULL<< 0);		//设置反相低电平有效
    pwm_start(); 							//PWM 更新参数

	while (1)
	{
		for(i=0; i<duties; i++)
		{
			pwm_set_duty(0, i);             	//设置PWM亮度
            pwm_start();                     	//使修改占空比生效，需要再次开启
            vTaskDelay(10 / portTICK_RATE_MS);
		}
		for(i=duties; i>0; i--)
		{
			pwm_set_duty(0, i);             	//设置PWM亮度
            pwm_start();                     	//使修改占空比生效，需要再次开启
            vTaskDelay(10 / portTICK_RATE_MS);
		}
	}
	vTaskDelete(NULL);
}


/**
 * @description:  读取数据进去nvs里面的任务
 * @param {type} 
 * @return: 
 */
static void Task_NVS_Read(void *pvParameters)
{

    ESP_LOGI(TAG, "--------------------------- Start Task_NVS_Read  --------------------------");

    nvs_handle mHandleNvsRead;          //NVS操作的句柄，类似于 rtos系统的任务创建返回的句柄！
    int8_t nvs_i8 = 0;

    esp_err_t err = nvs_open(TB_SELF, NVS_READWRITE, &mHandleNvsRead);
    //打开数据库，打开一个数据库就相当于会返回一个句柄
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Open NVS Table fail");
        vTaskDelete(NULL);
    }
    else
    {
        ESP_LOGI(TAG, "Open NVS Table ok.");
    }

    //读取 i8
    err = nvs_get_i8(mHandleNvsRead, FILED_SELF_i8, &nvs_i8);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "get nvs_i8 = %d ", nvs_i8);
    else
        ESP_LOGI(TAG, "get nvs_i8 error");

    //读取 字符串
    char data[65];
    uint32_t len = sizeof(data);
    err = nvs_get_str(mHandleNvsRead, FILED_SELF_Str, data, &len);

    if (err == ESP_OK)
        ESP_LOGI(TAG, "get str data = %s ", data);
    else
        ESP_LOGI(TAG, "get str data error");

    //读取数组
    uint8_t group_myself_read[8];
    size_t size = sizeof(group_myself_read);
    err = nvs_get_blob(mHandleNvsRead, FILED_SELF_Group, group_myself_read, &size);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "get group_myself_read data OK !");
        for (uint32_t i = 0; i < size; i++)
        {
            ESP_LOGI(TAG, "get group_myself_read data : [%d] =%02x", i, group_myself_read[i]);
        }
    }

    //读取结构体
    User_Info user;
    memset(&user, 0x0, sizeof(user));
    uint32_t length = sizeof(user);
    err = nvs_get_blob(mHandleNvsRead, FILED_SELF_Struct, &user, &length);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "get user Struct name = %s ", user.name);
        ESP_LOGI(TAG, "get user Struct age = %d ", user.age);
        ESP_LOGI(TAG, "get user Struct sex = %d ", user.sex);
    }

    //关闭数据库，关闭面板！
    nvs_close(mHandleNvsRead);

    ESP_LOGI(TAG, "--------------------------- End Task_NVS_Read  --------------------------");
    vTaskDelete(NULL);
}

/**
 * @description:  创建一个写数据进去nvs里面的任务
 * @param {type} 
 * @return: 
 */
static void Task_NVS_Write(void *pvParameters)
{

    ESP_LOGI(TAG, "--------------------------- Start Task_NVS_Write  --------------------------");

    nvs_handle mHandleNvs;      //NVS操作的句柄，类似于 rtos系统的任务创建返回的句柄！
    int8_t nvs_i8 = 11;         //注意int8_t的取值范围，根据自身的业务需求来做保存类型

    //打开数据库，打开一个数据库就相当于会返回一个句柄
    if (nvs_open(TB_SELF, NVS_READWRITE, &mHandleNvs) != ESP_OK)
    {
        ESP_LOGE(TAG, "Open NVS Table fail");
        vTaskDelete(NULL);
    }

    //保存一个 int8_t
    esp_err_t err = nvs_set_i8(mHandleNvs, FILED_SELF_i8, nvs_i8);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Save NVS i8 error !!");
    else
        ESP_LOGI(TAG, "Save NVS i8 ok !! nvs_i8 = %d ", nvs_i8);
		
    nvs_commit(mHandleNvs);     //提交下！相当于软件面板的 “应用” 按钮，并没关闭面板

    //自定义一个字符串
    char data[65] = {"hello world"};

    //保存一个字符串
    if (nvs_set_str(mHandleNvs, FILED_SELF_Str, data) != ESP_OK)
        ESP_LOGE(TAG, "Save NVS String Fail !!  ");
    else
        ESP_LOGI(TAG, "Save NVS String ok !! data : %s ", data);

    nvs_commit(mHandleNvs);     //提交下！相当于软件面板的 “应用” 按钮，并没关闭面板

    //自定义一个数组
    uint8_t group_myself[8] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};

    if (nvs_set_blob(mHandleNvs, FILED_SELF_Group, group_myself, sizeof(group_myself)) != ESP_OK)
        ESP_LOGE(TAG, "Save group  Fail !!  ");
    else
        ESP_LOGI(TAG, "Save group  ok !!  ");

    nvs_commit(mHandleNvs);     //提交下！相当于软件面板的 “应用” 按钮，并没关闭面板

    //保存一个结构体
    User_Info user = {
        .name = "shiwenkai",
        .age = 18,
        .sex = 10};

    if (nvs_set_blob(mHandleNvs, FILED_SELF_Struct, &user, sizeof(user)) != ESP_OK)
        ESP_LOGE(TAG, "Save Struct  Fail !!  ");
    else
        ESP_LOGI(TAG, "Save Struct  ok !!  ");

    nvs_commit(mHandleNvs);     //提交下！相当于软件面板的 “应用” 按钮，并没关闭面板

    nvs_close(mHandleNvs);      //关闭数据库，关闭面板！

    ESP_LOGI(TAG, "---------------------------End  Task_NVS_Write  --------------------------\n");
    //创建一个读取的任务
    xTaskCreate(Task_NVS_Read, "Task_NVS_Read", 1024, NULL, 5, NULL);

    vTaskDelay(500 / portTICK_RATE_MS);
    vTaskDelete(NULL);
}

