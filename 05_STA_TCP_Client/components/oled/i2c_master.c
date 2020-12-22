#include "i2c_master.h"
#include "driver/gpio.h"

#define i2c_master_wait     os_delay_us
#define i2c_delay_times     5

/* IIC初始化( SCL == IO14、SDA == IO2 ) */
void i2c_master_gpio_init(void)
{
    /* 定义一个gpio配置结构体 */
    gpio_config_t gpio_config_structure;

    /* 配置 IIC_SDA - GPIO2 - 通用推挽输出*/
    gpio_config_structure.mode = GPIO_MODE_OUTPUT;                      //输出模式
    gpio_config_structure.pin_bit_mask = IIC_OUTPUT_PIN_SEL;            //引脚
    gpio_config_structure.pull_down_en = 0;                             //禁止下拉
    gpio_config_structure.pull_up_en = 0;                               //禁止上拉
    gpio_config_structure.intr_type = GPIO_INTR_DISABLE;                //禁止中断
    gpio_config(&gpio_config_structure);                                //根据设定参数初始化并使能

    IIC_SCL_OUT(1);       //SDA输出高电平
    IIC_SCL_OUT(1);       //SCL输出高电平
}

/* 设置 IIC_SDA 模式 */
void IIC_SDA_Mode_Change(uint8_t SDA_Mode)
{
    gpio_config_t gpio_config_structure;				 	    //创建一个GPIO结构体
    gpio_config_structure.intr_type = GPIO_INTR_DISABLE;	    //失能中断服务
    gpio_config_structure.mode = SDA_Mode;			        
    gpio_config_structure.pin_bit_mask = (1ULL<<IIC_SDA_PIN);   //指定要配置的GPIO
    gpio_config_structure.pull_down_en = 0;				        //不使能下拉
    gpio_config_structure.pull_up_en = 0;					    //不使能上拉
    gpio_config(&gpio_config_structure);  					    //配置GPIO
}

/* IIC产生起始信号 - 启动信号的函数 */
void i2c_master_start(void)
{
    IIC_SDA_Mode_Change(SDA_MODE_WRITE);	//SDA切换到输出模式
    IIC_SCL_OUT(1);
    IIC_SDA_OUT(1);
	
	i2c_master_wait(i2c_delay_times);       //延时5us
	IIC_SDA_OUT(0);
	i2c_master_wait(i2c_delay_times);
	IIC_SCL_OUT(0);		                    //保持占用总线，时钟线为低电平的时候，可以修改SDA的值
	i2c_master_wait(i2c_delay_times);
}

/* 产生IIC停止信号 - 停止信号的函数 */
void i2c_master_stop(void)
{
    IIC_SDA_Mode_Change(SDA_MODE_WRITE);	//SDA切换到输出模式
    
	IIC_SCL_OUT(1);
    IIC_SDA_OUT(0);
    
	i2c_master_wait(i2c_delay_times);
	
	IIC_SDA_OUT(1);
	
	i2c_master_wait(i2c_delay_times);
}

/* 主机产生ACK应答 */
void i2c_master_send_ack(void)
{
    IIC_SDA_Mode_Change(SDA_MODE_WRITE);	//SDA切换到输出模式

	IIC_SCL_OUT(0);
    IIC_SDA_OUT(0);
	i2c_master_wait(i2c_delay_times);
	
	IIC_SCL_OUT(1);
	i2c_master_wait(i2c_delay_times);
	
	IIC_SCL_OUT(0);
	i2c_master_wait(i2c_delay_times);
}
/* 主机不产生ACK应答 */
void i2c_master_send_nack(void)
{
    IIC_SDA_Mode_Change(SDA_MODE_WRITE);	//SDA切换到输出模式
    
	IIC_SCL_OUT(0);
    IIC_SDA_OUT(0);
	i2c_master_wait(i2c_delay_times);
	
	IIC_SDA_OUT(1);
	i2c_master_wait(i2c_delay_times);
	
	IIC_SCL_OUT(1);
	i2c_master_wait(i2c_delay_times);
	IIC_SCL_OUT(0);
	i2c_master_wait(i2c_delay_times);
}

/* 等待应答信号到来 */
uint8_t i2c_master_Wait_Ack(void)
{
	uint8_t ask = 0;
	
	IIC_SDA_Mode_Change(SDA_MODE_READ);	    //SDA切换到输入模式
	
	IIC_SCL_OUT(1);                         //拉高时钟线
	
	i2c_master_wait(i2c_delay_times);
	
	if(IIC_SDA_IN())			            //IIC_SDA_IN() = 1无应答
	{
    	ask = 1;
    }
	else 
    {
        ask = 0;
    }
	IIC_SCL_OUT(0);
	
	i2c_master_wait(i2c_delay_times);

	IIC_SDA_Mode_Change(SDA_MODE_WRITE);   //切换回输出模式
	
	return ask;
}

/* 读取1个字节 */
uint8_t i2c_master_readByte(uint8_t read_data)
{
	int8_t i = 0;
	uint8_t byte = 0;
	
	IIC_SDA_Mode_Change(SDA_MODE_READ);	//SDA切换到输入模式
	i2c_master_wait(i2c_delay_times);		
	
	for(i = 7; i >=0 ; i--)
	{
		IIC_SCL_OUT(1);                     //时钟线为高时，采集SDA数据电平
		i2c_master_wait(i2c_delay_times);		
		
		if(IIC_SDA_IN())
        {
			byte |= 1<<i;
        }
		IIC_SCL_OUT(0);
		i2c_master_wait(i2c_delay_times);		
	}
	
	IIC_SDA_Mode_Change(SDA_MODE_WRITE);	//SDA切换到输出模式
	
	if(read_data)
		i2c_master_send_ack();
	else
		i2c_master_send_nack();
	return byte;
}

/* IIC发送一个字节 */
void i2c_master_sentByte(uint8_t sent_data)
{
	int8_t i=0;
	
	IIC_SCL_OUT(0);
    IIC_SDA_OUT(0);
	
	i2c_master_wait(i2c_delay_times);
	
	for(i=7; i>=0; i--)
	{
		if(sent_data & (1<<i))
			IIC_SDA_OUT(1);
		else
			IIC_SDA_OUT(0);
		
		i2c_master_wait(i2c_delay_times);
		
		IIC_SCL_OUT(1);
		i2c_master_wait(i2c_delay_times);	
	
		IIC_SCL_OUT(0);
		i2c_master_wait(i2c_delay_times);		
	}
}
