#include "dht11.h"

static const char *TAG = "DTH11";

void DHT11_init(void)
{
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;			//禁止中断
    io_conf.mode = GPIO_MODE_OUTPUT;				//输出模式
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;		//引脚
    io_conf.pull_down_en = 0;						//禁止下拉
    io_conf.pull_up_en = 0;							//禁止下拉
    ESP_ERROR_CHECK(gpio_config(&io_conf));			//初始化并使能
    gpio_set_level(DTH11_DATA_PIN, 1);				//输出1
}

/** @brief chip_start
*
*   host   --high---|-----low------|-----high-----|
*                   |   less 18ms  |   10-40us    |
*
*   dth11  ---low---------|----high----------|----- low...(chip start ok)
*             80us-83us   |    80us-87us     |
*/
static int chip_start(void)
{
    int ret = 0;
    int timeout =0;

    ESP_ERROR_CHECK(gpio_set_direction(DTH11_DATA_PIN, GPIO_MODE_OUTPUT));	//设置GPIO5为输出模式 
    vTaskDelay(10 / portTICK_RATE_MS);
    gpio_set_level(DTH11_DATA_PIN, 0);	//输出低电平
    vTaskDelay(20 / portTICK_RATE_MS);	//等待超过18毫秒

    ESP_ERROR_CHECK(gpio_set_direction(DTH11_DATA_PIN, GPIO_MODE_INPUT));	//设置GPIO5为输入模式 
    do
	{
        usleep(15);
        ret = gpio_get_level(DTH11_DATA_PIN);	//输入
        if(timeout > 7)  	// 超过105us，没有回答，失败
        {
            return ret;
        }
        timeout++;
    }
    while(ret);

    usleep(80);
    do
    {
        usleep(20);
        ret = gpio_get_level(DTH11_DATA_PIN);	//输入
    }
    while(ret);	//等待引脚电平低

    return ret;
}

static void stop_chip(void)
{
    usleep(50); 		//延时50us
    ESP_ERROR_CHECK(gpio_set_direction(DTH11_DATA_PIN, GPIO_MODE_OUTPUT));	//设置GPIO5为输出模式 
    gpio_set_level(DTH11_DATA_PIN, 1);		//输出高电平
}

/** @brief 获取40位的原始数据
*
*   data '0':  ---low----|----high-----|---next bit--
*                 50us   |   23us-28us |

*   data '1':  ---low----|------------high-------------|-next bit-
*                 50us   |            68us-74us             |

*/
int get_raw_data(uint8_t *pdata, int len)
{
    int ret = 0;
    int i;
    int flag_high =0;
    uint8_t rdata =0;

    if(pdata != NULL)
    {
        for(i=0; i<len; i++)
        {
            flag_high = 0;
			/*
			 *	while语句先判断条件，满足时执行循环体。
			 *	do while语句先执行循环体，满足条件再执行
			 */
            /* 获取一位数据 */
            do  			//等待一位开始标志结束
            {
                usleep(5);
                ret = gpio_get_level(DTH11_DATA_PIN);	//输入
            }
            while(!ret);

            while(ret)
            {
                usleep(10);
                ret = gpio_get_level(DTH11_DATA_PIN);	//输入
                flag_high++;
            }

            if(flag_high > 3)// >30us 是数据“1”
            {
                rdata |= 1<<(7-i%8); //第一个位是最高位 i%8 eq. 0-7
            }

            if(i%8 == 7)
            {
                *pdata = rdata;
                pdata++;
                rdata = 0;
            }
        }
    }
    else  // 数据为空
    {
        ret =1;
    }
    return ret;
}

/* 获取温度和湿度数据 */
int get_dth_data(int *dtemperature, int *dhumidty)
{
    uint8_t data[5]= {0};
    uint8_t check_sum = 0;
    int ret = 0;

    /* 复位DHT11 */
    ret = chip_start();
    if(ret)
    {
        ESP_LOGE(TAG, "No connect sensor[%d].....\n", ret);
        return ret;//复位芯片失败
    }

    /* 获取40位的原始数据 */
    ret = get_raw_data(data, 40);
    if(ret)//获取数据失败
    {
        ESP_LOGE(TAG, "get_raw_data fail[%d].....\n", ret);
        return ret;
    }

    //stop
    stop_chip();

    /*  检查数据  */
    check_sum = (data[0] + data[1] + data[2] + data[3])&0xff;
    if(check_sum != data[4])
    {
        ret = 2;
        ESP_LOGE(TAG, "check data fail.....\n");	//失败
        return ret;
    }

    /* 转换湿度 */
    *dhumidty = data[0]*10 + data[1];
    if((data[3] & 0x80) == 0x80)  // 温度为负数
    {
        data[3] &= 0x7f; //消除负数位
        *dtemperature = -(data[2]*10 + data[3]);
    }
    else
    {
        *dtemperature = data[2]*10 + data[3];
    }
    return ret;
}

