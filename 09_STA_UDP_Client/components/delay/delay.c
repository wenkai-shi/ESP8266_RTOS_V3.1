#include "delay.h"
#include "rom/ets_sys.h"

// 微秒延时函数
void Delay_us(unsigned int us)
{
	os_delay_us(us);
}

// 毫秒延时函数
void Delay_ms(int C_time)
{	for(;C_time>0;C_time--)
		os_delay_us(1000);
}

