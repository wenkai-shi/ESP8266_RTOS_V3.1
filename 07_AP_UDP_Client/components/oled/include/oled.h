#ifndef _OLED_H_
#define _OLED_H_

#include <stdio.h>
#include "i2c_master.h"	// IIC
#include "delay.h"

// 宏定义
//=============================================================================
#define		OLED_CMD  	0		// 命令
#define		OLED_DATA 	1		// 数据

#define 	SIZE 		16		//显示字符的大小
#define 	Max_Column	128		//最大列数
#define		Max_Row		64		//最大行数
#define		X_WIDTH 	128		//X轴的宽度
#define		Y_WIDTH 	64	    //Y轴的宽度
#define		IIC_ACK		0		//应答
#define		IIC_NO_ACK	1		//不应答
//=============================================================================

// 函数声明
//=============================================================================
void IIC_Init(void);
uint8_t OLED_Write_Command(uint8_t OLED_Byte);
uint8_t OLED_Write_Data(uint8_t OLED_Byte);
void OLED_WR_Byte(uint8_t OLED_Byte, uint8_t OLED_Type);
void OLED_Clear(void);
void OLED_Set_Pos(uint8_t x, uint8_t y);
void OLED_Init(void);
void OLED_ShowChar(uint8_t x, uint8_t y, uint8_t Show_char);
void OLED_ShowString(uint8_t x, uint8_t y, uint8_t * Show_char);
void OLED_ShowIP(uint8_t x, uint8_t y, uint8_t*Array_IP);


#endif /* OLED_H_ */
