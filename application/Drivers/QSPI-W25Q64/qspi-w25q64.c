/****
	***********************************************************************************************************************************************************************************
	*	@file  	qspi_w25q64.c
	*	@version V1.0
	*  @date    2022-7-12
	*	@author  反客科技
	*	@brief   QSPI驱动W25Qxx相关函数，提供的读写函数均使用HAL库函数直接操作
   ************************************************************************************************************************************************************************************
   *  @description
	*
	*	实验平台：反客STM32H750XBH6核心板 （型号：FK750M5-XBH6）
	*	淘宝地址：https://shop212360197.taobao.com
	*	QQ交流群：536665479
	*
>>>>> 文件说明：
	*
	*  1.例程参考于官方驱动文件 stm32h743i_eval_qspi.c
	*	2.例程使用的是 QUADSPI_BK1
	*	3.提供的读写函数均使用HAL库函数直接操作，没有用到DMA和中断
	*	4.默认配置QSPI驱动时钟为120M
	*
>>>>> 重要说明：
	*
	*	1.W25QXX的擦除时间是限定的!!! 手册给出的典型参考值为: 4K-45ms, 32K-120ms ,64K-150ms,整片擦除20S
	*
	*	2.W25QXX的写入时间是限定的!!! 手册给出的典型参考值为: 256字节-0.4ms，也就是 1M字节/s （实测大概在600K字节/s左右）
	*
	*	3.如果使用库函数直接读取，那么是否使用DMA、是否开启Cache、编译器的优化等级以及数据存储区的位置(内部 TCM SRAM 或者 AXI SRAM)都会影响读取的速度
	*
	*	4.如果使用内存映射模式，则读取性能只与QSPI的驱动时钟以及是否开启Cache有关
	*
	*	5.使用库函数进行直接读取，keil版本5.30，编译器AC6.14，编译等级Oz image size，读取速度为 7M字节/S ，数据放在TCM SRAM 或者 AXI SRAM  
	*    都是差不多的结果，因为CPU直接访问外设寄存器的效率很低，直接使用HAL库进行读取的话，速度很慢
	*
	*	6.如果使用MDMA进行读取，可以达到 58M字节/S，使用内存映射模式的话，几乎可以达到驱动时钟的全速，62.14M/s (133MHz时钟下)
	*
	*  7.W25Q64JV 所允许的最高驱动频率为133MHz，750的QSPI最高驱动频率也是133MHz ，但是对于HAL库函数直接读取而言，驱动时钟超过15M已经不会有性能提升
	*
	*	8.对于内存映射模式直接读取而言，驱动时钟超过127.5M已经不会有性能提升，因为QSPI内核时钟最高限定为250M，所以建议实际QSPI驱动时钟不要超过125M，
	*	  具体的时钟配置请参考 SystemClock_Config 函数
	*
	*	9.实际使用中，当数据比较大时，建议使用64K或者32K擦除，擦除时间比4K擦除块	
	*
	**************************************************************************************************************************************************************************************FANKE*****
***/

#include "qspi-w25q64.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

QSPI_HandleTypeDef hqspi;
static bool xip_enabled = false;  // 跟踪XIP模式状态

/**
 * @brief 
 * 函数功能: QSPI引脚初始化函数
 * 说    明: 该函数会被	MX_QUADSPI_Init 函数调用
 * 
 * @param hqspi 	QSPI_HandleTypeDef定义的变量，即表示定义的QSPI句柄
 */
void HAL_QSPI_MspInit(QSPI_HandleTypeDef* hqspi)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	if(hqspi->Instance==QUADSPI)
	{
		__HAL_RCC_QSPI_CLK_ENABLE();		// 使能QSPI时钟
		__HAL_RCC_QSPI_FORCE_RESET();		// 复位QSPI
		__HAL_RCC_QSPI_RELEASE_RESET();		
		
		GPIO_QUADSPI_CLK_ENABLE;		// 使能 QUADSPI_CLK IO口时钟
		GPIO_QUADSPI_BK1_NCS_ENABLE;	// 使能 QUADSPI_BK1_NCS IO口时钟
		GPIO_QUADSPI_BK1_IO0_ENABLE;	// 使能 QUADSPI_BK1_IO0 IO口时钟
		GPIO_QUADSPI_BK1_IO1_ENABLE;	// 使能 QUADSPI_BK1_IO1 IO口时钟
		GPIO_QUADSPI_BK1_IO2_ENABLE;	// 使能 QUADSPI_BK1_IO2 IO口时钟
		GPIO_QUADSPI_BK1_IO3_ENABLE;	// 使能 QUADSPI_BK1_IO3 IO口时钟
		
		/******************************************************  
		 PG6     ------> QUADSPI_BK1_NCS
		 PF6     ------> QUADSPI_BK1_IO3
		 PF7     ------> QUADSPI_BK1_IO2
		 PF8     ------> QUADSPI_BK1_IO0
		 PF10     ------> QUADSPI_CLK
		 PF9     ------> QUADSPI_BK1_IO1
		*******************************************************/
		
		GPIO_InitStruct.Mode 		= GPIO_MODE_AF_PP;				// 复用推挽输出模式
		GPIO_InitStruct.Pull 		= GPIO_NOPULL;					// 无上下拉
		GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;	// 超高速IO口速度
		
		GPIO_InitStruct.Pin 		= QUADSPI_CLK_PIN;				// QUADSPI_CLK 引脚
		GPIO_InitStruct.Alternate 	= QUADSPI_CLK_AF;				// QUADSPI_CLK 复用
		HAL_GPIO_Init(QUADSPI_CLK_PORT, &GPIO_InitStruct);			// 初始化 QUADSPI_CLK 引脚

		GPIO_InitStruct.Pin 		= QUADSPI_BK1_NCS_PIN;			// QUADSPI_BK1_NCS 引脚
		GPIO_InitStruct.Alternate 	= QUADSPI_BK1_NCS_AF;			// QUADSPI_BK1_NCS 复用
		HAL_GPIO_Init(QUADSPI_BK1_NCS_PORT, &GPIO_InitStruct);   	// 初始化 QUADSPI_BK1_NCS 引脚
		
		GPIO_InitStruct.Pin 		= QUADSPI_BK1_IO0_PIN;			// QUADSPI_BK1_IO0 引脚
		GPIO_InitStruct.Alternate 	= QUADSPI_BK1_IO0_AF;			// QUADSPI_BK1_IO0 复用
		HAL_GPIO_Init(QUADSPI_BK1_IO0_PORT, &GPIO_InitStruct);		// 初始化 QUADSPI_BK1_IO0 引脚	
		
		GPIO_InitStruct.Pin 		= QUADSPI_BK1_IO1_PIN;			// QUADSPI_BK1_IO1 引脚
		GPIO_InitStruct.Alternate 	= QUADSPI_BK1_IO1_AF;			// QUADSPI_BK1_IO1 复用
		HAL_GPIO_Init(QUADSPI_BK1_IO1_PORT, &GPIO_InitStruct);   	// 初始化 QUADSPI_BK1_IO1 引脚
		
		GPIO_InitStruct.Pin 		= QUADSPI_BK1_IO2_PIN;			// QUADSPI_BK1_IO2 引脚
		GPIO_InitStruct.Alternate 	= QUADSPI_BK1_IO2_AF;			// QUADSPI_BK1_IO2 复用
		HAL_GPIO_Init(QUADSPI_BK1_IO2_PORT, &GPIO_InitStruct);		// 初始化 QUADSPI_BK1_IO2 引脚			
		
		GPIO_InitStruct.Pin 		= QUADSPI_BK1_IO3_PIN;			// QUADSPI_BK1_IO3 引脚
		GPIO_InitStruct.Alternate 	= QUADSPI_BK1_IO3_AF;			// QUADSPI_BK1_IO3 复用
		HAL_GPIO_Init(QUADSPI_BK1_IO3_PORT, &GPIO_InitStruct);		// 初始化 QUADSPI_BK1_IO3 引脚
	}
}

/**
 * @brief 
 * 函数功能: 初始化 QSPI 配置
 */
void MX_QUADSPI_Init(void)
{
//		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植
/*在某些场合，例如用作下载算法时，需要手动清零句柄等参数，否则会工作不正常*/	
	uint32_t i;
	char *p;

	/* 此处参考安富莱的代码，大家可以去学习安富莱的教程，非常专业 */
	p = (char *)&hqspi;
	for (i = 0; i < sizeof(hqspi); i++)
	{
		*p++ = 0;
	}
	hqspi.Instance  = QUADSPI;
	HAL_QSPI_DeInit(&hqspi) ;		// 复位QSPI
/********************/

	hqspi.Instance 					= QUADSPI;							// QSPI外设

	/*本例程选择 HCLK 作为QSPI的内核时钟，速度为240M，再经过2分频得到120M驱动时钟，
	  关于 QSPI内核时钟 的设置，请参考 main.c文件里的 SystemClock_Config 函数*/
	// 需要注意的是，当使用内存映射模式时，这里的分频系数不能设置为0！！否则会读取错误
	hqspi.Init.ClockPrescaler 		= 1;								// 主频为480，在QUASPI频率为240的情况下，需要设置ClockPrescaler项为6（至少为6）。
	hqspi.Init.FifoThreshold 		= 32;								// FIFO阈值
	hqspi.Init.SampleShifting		= QSPI_SAMPLE_SHIFTING_HALFCYCLE;	// 半个CLK周期之后进行采样
	hqspi.Init.FlashSize 			= 22;								// flash大小，FLASH 中的字节数 = 2^[FSIZE+1]，核心板采用是8M字节的W25Q64，这里设置为22
	hqspi.Init.ChipSelectHighTime 	= QSPI_CS_HIGH_TIME_1_CYCLE;		// 片选保持高电平的时间
	hqspi.Init.ClockMode 			= QSPI_CLOCK_MODE_3;				// 模式3
	hqspi.Init.FlashID 				= QSPI_FLASH_ID_1;					// 使用QSPI1
	hqspi.Init.DualFlash 			= QSPI_DUALFLASH_DISABLE;			// 禁止双闪存模式


	HAL_QSPI_Init(&hqspi); // 初始化配置

}

/**
 * @brief 
 * 函数功能: 初始化 QSPI 配置，读取W25Q64ID
 * 
 * @return int8_t 
 * QSPI_W25Qxx_OK - 初始化成功，W25Qxx_ERROR_INIT - 初始化错误
 */
int8_t QSPI_W25Qxx_Init(void)
{
	uint32_t Device_ID;
	
	MX_QUADSPI_Init();
	QSPI_W25Qxx_Reset();
	Device_ID = QSPI_W25Qxx_ReadID();
	
	if(Device_ID == W25Qxx_FLASH_ID)
	{	
		return QSPI_W25Qxx_OK;
	}
	else
	{
		QSPI_W25Qxx_DBG("W25Q64 ERROR!!!!!  ID:%X", (unsigned int)Device_ID);
		return W25Qxx_ERROR_INIT;
	}
}

/**
 * @brief 
 * 函数功能: 使用自动轮询标志查询，等待通信结束
 * 说    明: 每一次通信都应该调用次函数，等待通信结束，避免错误的操作	
 * 
 * @return int8_t 
 * QSPI_W25Qxx_OK - 通信正常结束，W25Qxx_ERROR_AUTOPOLLING - 轮询等待无响应
 */
int8_t QSPI_W25Qxx_AutoPollingMemReady(void)
{
		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植

	QSPI_CommandTypeDef     s_command;	   // QSPI传输配置
	QSPI_AutoPollingTypeDef s_config;		// 轮询比较相关配置参数

	s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;			// 1线指令模式
	s_command.AddressMode       = QSPI_ADDRESS_NONE;				// 无地址模式
	s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;		//	无交替字节 
	s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;	     	// 禁止DDR模式
	s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;	   	// DDR模式中数据延迟，这里用不到
	s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;	   		//	每次传输数据都发送指令	
	s_command.DataMode          = QSPI_DATA_1_LINE;					// 1线数据模式
	s_command.DummyCycles       = 0;								//	空周期个数
	s_command.Instruction       = W25Qxx_CMD_ReadStatus_REG1;	   	// 读状态信息寄存器
																					
// 不停的查询 W25Qxx_CMD_ReadStatus_REG1 寄存器，将读取到的状态字节中的 W25Qxx_Status_REG1_BUSY 不停的与0作比较
// 读状态寄存器1的第0位（只读），Busy标志位，当正在擦除/写入数据/写命令时会被置1，空闲或通信结束为0
	
	s_config.Match           = 0;   								//	匹配值
	s_config.MatchMode       = QSPI_MATCH_MODE_AND;	      			//	与运算
	s_config.Interval        = 0x10;	                     		//	轮询间隔
	s_config.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;			// 自动停止模式
	s_config.StatusBytesSize = 1;	                        		//	状态字节数
	s_config.Mask            = W25Qxx_Status_REG1_BUSY;	   			// 对在轮询模式下接收的状态字节进行屏蔽，只比较需要用到的位
		
	// 发送轮询等待命令
	if (HAL_QSPI_AutoPolling(&hqspi, &s_command, &s_config, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
	{
		return W25Qxx_ERROR_AUTOPOLLING; // 轮询等待无响应
	}
	return QSPI_W25Qxx_OK; // 通信正常结束

}

/**
 * @brief 
 * 函数功能: 复位器件
 * 
 * @return int8_t 
 * QSPI_W25Qxx_OK - 复位成功，W25Qxx_ERROR_INIT - 初始化错误
 */
int8_t QSPI_W25Qxx_Reset(void)	
{
		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植

	QSPI_CommandTypeDef s_command;	// QSPI传输配置

	s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;   	// 1线指令模式
	s_command.AddressMode 		= QSPI_ADDRESS_NONE;   			// 无地址模式
	s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE; 	// 无交替字节 
	s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;     	// 禁止DDR模式
	s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY; 	// DDR模式中数据延迟，这里用不到
	s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;	 	// 每次传输数据都发送指令
	s_command.DataMode 			= QSPI_DATA_NONE;       		// 无数据模式	
	s_command.DummyCycles 		= 0;                     		// 空周期个数
	s_command.Instruction 		= W25Qxx_CMD_EnableReset;      	// 执行复位使能命令

	// 发送复位使能命令
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) 
	{
		return W25Qxx_ERROR_INIT;			// 如果发送失败，返回错误信息
	}
	// 使用自动轮询标志位，等待通信结束
	if (QSPI_W25Qxx_AutoPollingMemReady() != QSPI_W25Qxx_OK)
	{
		return W25Qxx_ERROR_AUTOPOLLING;	// 轮询等待无响应
	}

	s_command.Instruction  = W25Qxx_CMD_ResetDevice;     // 复位器件命令    

	//发送复位器件命令
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) 
	{
		return W25Qxx_ERROR_INIT;		  // 如果发送失败，返回错误信息
	}
	// 使用自动轮询标志位，等待通信结束
	if (QSPI_W25Qxx_AutoPollingMemReady() != QSPI_W25Qxx_OK)
	{
		return W25Qxx_ERROR_AUTOPOLLING;	// 轮询等待无响应
	}	
	return QSPI_W25Qxx_OK;	// 复位成功
}

/**
 * @brief 
 * 函数功能: 初始化 QSPI 配置，读取器件ID
 * 
 * @return uint32_t 
 * W25Qxx_ID - 读取到的器件ID，W25Qxx_ERROR_INIT - 通信、初始化错误
 */
uint32_t QSPI_W25Qxx_ReadID(void)	
{
		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植

	QSPI_CommandTypeDef s_command;	// QSPI传输配置
	uint8_t	QSPI_ReceiveBuff[3];		// 存储QSPI读到的数据
	uint32_t	W25Qxx_ID;					// 器件的ID

	s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;    	// 1线指令模式
	s_command.AddressSize       = QSPI_ADDRESS_24_BITS;     	// 24位地址
	s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;  	// 无交替字节 
	s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;      	// 禁止DDR模式
	s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;  	// DDR模式中数据延迟，这里用不到
	s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;	 	// 每次传输数据都发送指令
	s_command.AddressMode		= QSPI_ADDRESS_NONE;   			// 无地址模式
	s_command.DataMode			= QSPI_DATA_1_LINE;       	 	// 1线数据模式
	s_command.DummyCycles 		= 0;                   			// 空周期个数
	s_command.NbData 			= 3;                       		// 传输数据的长度
	s_command.Instruction 		= W25Qxx_CMD_JedecID;         	// 执行读器件ID命令

	// 发送指令
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) 
	{
		QSPI_W25Qxx_DBG("HAL_QSPI_Command failure! ");
//		return W25Qxx_ERROR_INIT;		// 如果发送失败，返回错误信息
	}
	// 接收数据
	if (HAL_QSPI_Receive(&hqspi, QSPI_ReceiveBuff, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) 
	{
		QSPI_W25Qxx_DBG("HAL_QSPI_Receive failure!");
//		return W25Qxx_ERROR_TRANSMIT;  // 如果接收失败，返回错误信息
	}
	// 将得到的数据组合成ID
	W25Qxx_ID = (QSPI_ReceiveBuff[0] << 16) | (QSPI_ReceiveBuff[1] << 8 ) | QSPI_ReceiveBuff[2];

	return W25Qxx_ID; // 返回ID
}

/**
 * @brief 
 * 函数功能: 发送写使能命令
 * 
 * @return int8_t 
 * QSPI_W25Qxx_OK - 写使能成功，W25Qxx_ERROR_WriteEnable - 写使能失败
 */
int8_t QSPI_W25Qxx_WriteEnable(void)
{
		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植

	QSPI_CommandTypeDef     s_command;	   // QSPI传输配置
	QSPI_AutoPollingTypeDef s_config;		// 轮询比较相关配置参数

	s_command.InstructionMode   	= QSPI_INSTRUCTION_1_LINE;    	// 1线指令模式
	s_command.AddressMode 			= QSPI_ADDRESS_NONE;   		      // 无地址模式
	s_command.AlternateByteMode 	= QSPI_ALTERNATE_BYTES_NONE;  	// 无交替字节 
	s_command.DdrMode           	= QSPI_DDR_MODE_DISABLE;      	// 禁止DDR模式
	s_command.DdrHoldHalfCycle  	= QSPI_DDR_HHC_ANALOG_DELAY;  	// DDR模式中数据延迟，这里用不到
	s_command.SIOOMode          	= QSPI_SIOO_INST_EVERY_CMD;		// 每次传输数据都发送指令	
	s_command.DataMode 				= QSPI_DATA_NONE;       	      // 无数据模式
	s_command.DummyCycles 			= 0;                   	         // 空周期个数
	s_command.Instruction	 		= W25Qxx_CMD_WriteEnable;      	// 发送写使能命令

	// 发送写使能命令
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) 
	{
		return W25Qxx_ERROR_WriteEnable;	//
	}
	
// 不停的查询 W25Qxx_CMD_ReadStatus_REG1 寄存器，将读取到的状态字节中的 W25Qxx_Status_REG1_WEL 不停的与 0x02 作比较
// 读状态寄存器1的第1位（只读），WEL写使能标志位，该标志位为1时，代表可以进行写操作
	
	s_config.Match           = 0x02;  								// 匹配值
	s_config.Mask            = W25Qxx_Status_REG1_WEL;	 		// 读状态寄存器1的第1位（只读），WEL写使能标志位，该标志位为1时，代表可以进行写操作
	s_config.MatchMode       = QSPI_MATCH_MODE_AND;			 	// 与运算
	s_config.StatusBytesSize = 1;									 	// 状态字节数
	s_config.Interval        = 0x10;							 		// 轮询间隔
	s_config.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;	// 自动停止模式

	s_command.Instruction    = W25Qxx_CMD_ReadStatus_REG1;	// 读状态信息寄存器
	s_command.DataMode       = QSPI_DATA_1_LINE;					// 1线数据模式
	s_command.NbData         = 1;										// 数据长度

	// 发送轮询等待命令	
	if (HAL_QSPI_AutoPolling(&hqspi, &s_command, &s_config, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
	{
		return W25Qxx_ERROR_AUTOPOLLING; 	// 轮询等待无响应
	}	
	return QSPI_W25Qxx_OK;  // 通信正常结束
}

/**
 * @brief 
 * 函数功能: 进行扇区擦除操作，每次擦除4K字节
 * 1.按照 W25Q64JV 数据手册给出的擦除参考时间，典型值为 45ms，最大值为400ms
 * 2.实际的擦除速度可能大于45ms，也可能小于45ms
 * 3.flash使用的时间越长，擦除所需时间也会越长
 * 
 * @param SectorAddress 		要擦除的地址
 * @return int8_t 
 * QSPI_W25Qxx_OK - 擦除成功
 * W25Qxx_ERROR_Erase - 擦除失败
 * W25Qxx_ERROR_AUTOPOLLING - 轮询等待无响应
 */
int8_t QSPI_W25Qxx_SectorErase(uint32_t SectorAddress)	
{
		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植

	QSPI_CommandTypeDef s_command;	// QSPI传输配置
	
	s_command.InstructionMode   	= QSPI_INSTRUCTION_1_LINE;    // 1线指令模式
	s_command.AddressSize       	= QSPI_ADDRESS_24_BITS;       // 24位地址模式
	s_command.AlternateByteMode 	= QSPI_ALTERNATE_BYTES_NONE;  //	无交替字节 
	s_command.DdrMode           	= QSPI_DDR_MODE_DISABLE;      // 禁止DDR模式
	s_command.DdrHoldHalfCycle  	= QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
	s_command.SIOOMode          	= QSPI_SIOO_INST_EVERY_CMD;	// 每次传输数据都发送指令
	s_command.AddressMode 			= QSPI_ADDRESS_1_LINE;        // 1线地址模式
	s_command.DataMode 				= QSPI_DATA_NONE;             // 无数据
	s_command.DummyCycles 			= 0;                          // 空周期个数
	s_command.Address           	= SectorAddress;              // 要擦除的地址
	s_command.Instruction	 		= W25Qxx_CMD_SectorErase;     // 扇区擦除命令

	// 发送写使能
	if (QSPI_W25Qxx_WriteEnable() != QSPI_W25Qxx_OK)
	{
		QSPI_W25Qxx_ERR("QSPI_W25Qxx_SectorErase: QSPI_W25Qxx_WriteEnable failure!");
		return W25Qxx_ERROR_WriteEnable;		// 写使能失败
	}
	// 发出擦除命令
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
	{
		QSPI_W25Qxx_ERR("QSPI_W25Qxx_SectorErase: HAL_QSPI_Command failure!");
		return W25Qxx_ERROR_Erase;				// 擦除失败
	}
	// 使用自动轮询标志位，等待擦除的结束 
	if (QSPI_W25Qxx_AutoPollingMemReady() != QSPI_W25Qxx_OK)
	{
		QSPI_W25Qxx_ERR("QSPI_W25Qxx_SectorErase: QSPI_W25Qxx_AutoPollingMemReady failure!");
		return W25Qxx_ERROR_AUTOPOLLING;		// 轮询等待无响应
	}
	return QSPI_W25Qxx_OK; // 擦除成功
}

/**
 * @brief 
 * 函数功能: 进行块擦除操作，每次擦除32K字节
 * 1.按照 W25Q64JV 数据手册给出的擦除参考时间，典型值为 120ms，最大值为1600ms
 * 2.实际的擦除速度可能大于120ms，也可能小于120ms
 * 3.flash使用的时间越长，擦除所需时间也会越长
 * 
 * @param SectorAddress 		要擦除的地址
 * @return int8_t 
 * QSPI_W25Qxx_OK - 擦除成功
 * W25Qxx_ERROR_Erase - 擦除失败
 * W25Qxx_ERROR_AUTOPOLLING - 轮询等待无响应
 */
int8_t QSPI_W25Qxx_BlockErase_32K (uint32_t SectorAddress)	
{
		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植

	QSPI_CommandTypeDef s_command;	// QSPI传输配置
	
	s_command.InstructionMode   	= QSPI_INSTRUCTION_1_LINE;    // 1线指令模式
	s_command.AddressSize       	= QSPI_ADDRESS_24_BITS;       // 24位地址模式
	s_command.AlternateByteMode 	= QSPI_ALTERNATE_BYTES_NONE;  //	无交替字节 
	s_command.DdrMode           	= QSPI_DDR_MODE_DISABLE;      // 禁止DDR模式
	s_command.DdrHoldHalfCycle  	= QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
	s_command.SIOOMode          	= QSPI_SIOO_INST_EVERY_CMD;	// 每次传输数据都发送指令
	s_command.AddressMode 			= QSPI_ADDRESS_1_LINE;        // 1线地址模式
	s_command.DataMode 				= QSPI_DATA_NONE;             // 无数据
	s_command.DummyCycles 			= 0;                          // 空周期个数
	s_command.Address           	= SectorAddress;              // 要擦除的地址
	s_command.Instruction	 		= W25Qxx_CMD_BlockErase_32K;  // 块擦除命令，每次擦除32K字节

	// 发送写使能	
	if (QSPI_W25Qxx_WriteEnable() != QSPI_W25Qxx_OK)
	{
		return W25Qxx_ERROR_WriteEnable;		// 写使能失败
	}
	// 发出擦除命令
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
	{
		return W25Qxx_ERROR_Erase;				// 擦除失败
	}
	// 使用自动轮询标志位，等待擦除的结束 
	if (QSPI_W25Qxx_AutoPollingMemReady() != QSPI_W25Qxx_OK)
	{
		return W25Qxx_ERROR_AUTOPOLLING;		// 轮询等待无响应
	}
	return QSPI_W25Qxx_OK;	// 擦除成功
}

/**
 * @brief 
 * 函数功能: 进行块擦除操作，每次擦除64K字节
 * 说    明: 1.按照 W25Q64JV 数据手册给出的擦除参考时间，典型值为 150ms，最大值为2000ms
 *			2.实际的擦除速度可能大于150ms，也可能小于150ms
 *			3.flash使用的时间越长，擦除所需时间也会越长
 *			4.实际使用建议使用64K擦除，擦除的时间最快
 * 
 * @param SectorAddress 		要擦除的地址
 * @return int8_t 
 *  QSPI_W25Qxx_OK - 擦除成功
 *  W25Qxx_ERROR_Erase - 擦除失败
 *	W25Qxx_ERROR_AUTOPOLLING - 轮询等待无响应
 */
int8_t QSPI_W25Qxx_BlockErase_64K (uint32_t SectorAddress)	
{
		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植

	QSPI_CommandTypeDef s_command;	// QSPI传输配置
	
	s_command.InstructionMode   	= QSPI_INSTRUCTION_1_LINE;    // 1线指令模式
	s_command.AddressSize       	= QSPI_ADDRESS_24_BITS;       // 24位地址模式
	s_command.AlternateByteMode 	= QSPI_ALTERNATE_BYTES_NONE;  //	无交替字节 
	s_command.DdrMode           	= QSPI_DDR_MODE_DISABLE;      // 禁止DDR模式
	s_command.DdrHoldHalfCycle  	= QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
	s_command.SIOOMode          	= QSPI_SIOO_INST_EVERY_CMD;	// 每次传输数据都发送指令
	s_command.AddressMode 			= QSPI_ADDRESS_1_LINE;        // 1线地址模式
	s_command.DataMode 				= QSPI_DATA_NONE;             // 无数据
	s_command.DummyCycles 			= 0;                          // 空周期个数
	s_command.Address           	= SectorAddress;              // 要擦除的地址
	s_command.Instruction	 		= W25Qxx_CMD_BlockErase_64K;  // 块擦除命令，每次擦除64K字节	

	// 发送写使能
	if (QSPI_W25Qxx_WriteEnable() != QSPI_W25Qxx_OK)
	{
		return W25Qxx_ERROR_WriteEnable;	// 写使能失败
	}
	// 发出擦除命令
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
	{
		return W25Qxx_ERROR_Erase;			// 擦除失败
	}
	// 使用自动轮询标志位，等待擦除的结束 
	if (QSPI_W25Qxx_AutoPollingMemReady() != QSPI_W25Qxx_OK)
	{
		return W25Qxx_ERROR_AUTOPOLLING;	// 轮询等待无响应
	}
	return QSPI_W25Qxx_OK;		// 擦除成功
}

/**
 * @brief 
 * 函数功能: 进行整片擦除操作
 * 
 * @return int8_t 
 * 1.按照 W25Q64JV 数据手册给出的擦除参考时间，典型值为 20s，最大值为100s
 * 2.实际的擦除速度可能大于20s，也可能小于20s
 * 3.flash使用的时间越长，擦除所需时间也会越长
 */
int8_t QSPI_W25Qxx_ChipErase (void)	
{
		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植

	QSPI_CommandTypeDef s_command;		// QSPI传输配置
	QSPI_AutoPollingTypeDef s_config;	// 轮询等待配置参数

	s_command.InstructionMode   	= QSPI_INSTRUCTION_1_LINE;    // 1线指令模式
	s_command.AddressSize       	= QSPI_ADDRESS_24_BITS;       // 24位地址模式
	s_command.AlternateByteMode 	= QSPI_ALTERNATE_BYTES_NONE;  //	无交替字节 
	s_command.DdrMode           	= QSPI_DDR_MODE_DISABLE;      // 禁止DDR模式
	s_command.DdrHoldHalfCycle  	= QSPI_DDR_HHC_ANALOG_DELAY;  // DDR模式中数据延迟，这里用不到
	s_command.SIOOMode          	= QSPI_SIOO_INST_EVERY_CMD;	// 每次传输数据都发送指令
	s_command.AddressMode 			= QSPI_ADDRESS_NONE;       	// 无地址
	s_command.DataMode 				= QSPI_DATA_NONE;             // 无数据
	s_command.DummyCycles 			= 0;                          // 空周期个数
	s_command.Instruction	 		= W25Qxx_CMD_ChipErase;       // 擦除命令，进行整片擦除

	// 发送写使能	
	if (QSPI_W25Qxx_WriteEnable() != QSPI_W25Qxx_OK)
	{
		return W25Qxx_ERROR_WriteEnable;	// 写使能失败
	}
	// 发出擦除命令
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
	{
		return W25Qxx_ERROR_Erase;		 // 擦除失败
	}

// 不停的查询 W25Qxx_CMD_ReadStatus_REG1 寄存器，将读取到的状态字节中的 W25Qxx_Status_REG1_BUSY 不停的与0作比较
// 读状态寄存器1的第0位（只读），Busy标志位，当正在擦除/写入数据/写命令时会被置1，空闲或通信结束为0
	
	s_config.Match           = 0;   									//	匹配值
	s_config.MatchMode       = QSPI_MATCH_MODE_AND;	      	//	与运算
	s_config.Interval        = 0x10;	                     	//	轮询间隔
	s_config.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;	// 自动停止模式
	s_config.StatusBytesSize = 1;	                        	//	状态字节数
	s_config.Mask            = W25Qxx_Status_REG1_BUSY;	   // 对在轮询模式下接收的状态字节进行屏蔽，只比较需要用到的位
	
	s_command.Instruction    = W25Qxx_CMD_ReadStatus_REG1;	// 读状态信息寄存器
	s_command.DataMode       = QSPI_DATA_1_LINE;					// 1线数据模式
	s_command.NbData         = 1;										// 数据长度

	// W25Q64整片擦除的典型参考时间为20s，最大时间为100s，这里的超时等待值 W25Qxx_ChipErase_TIMEOUT_MAX 为 100S
	if (HAL_QSPI_AutoPolling(&hqspi, &s_command, &s_config, W25Qxx_ChipErase_TIMEOUT_MAX) != HAL_OK)
	{
		return W25Qxx_ERROR_AUTOPOLLING;	 // 轮询等待无响应
	}
	return QSPI_W25Qxx_OK;
}

/**
 * @brief 
 * 函数功能: 按页写入，最大只能256字节，在数据写入之前，请务必完成擦除操作
 * 说    明: 1.Flash的写入时间和擦除时间一样，是限定的，并不是说QSPI驱动时钟133M就可以以这个速度进行写入
 *			2.按照 W25Q64JV 数据手册给出的 页(256字节) 写入参考时间，典型值为 0.4ms，最大值为3ms
 *			3.实际的写入速度可能大于0.4ms，也可能小于0.4ms
 *			4.Flash使用的时间越长，写入所需时间也会越长
 *			5.在数据写入之前，请务必完成擦除操作
 * 
 * @param pBuffer 			要写入的数据
 * @param WriteAddr 		要写入 W25Qxx 的地址
 * @param NumByteToWrite 	数据长度，最大只能256字节
 * @return int8_t  			QSPI_W25Qxx_OK 		     - 写数据成功
 *			    			W25Qxx_ERROR_WriteEnable - 写使能失败
 *				 			W25Qxx_ERROR_TRANSMIT	 - 传输失败
 *				 			W25Qxx_ERROR_AUTOPOLLING - 轮询等待无响应
 */
int8_t QSPI_W25Qxx_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
		// 定义QSPI句柄，这里保留使用cubeMX生成的变量命名，方便用户参考和移植

	QSPI_CommandTypeDef s_command;	// QSPI传输配置	
	
	s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;    		// 1线指令模式
	s_command.AddressSize       = QSPI_ADDRESS_24_BITS;            // 24位地址
	s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;  		// 无交替字节 
	s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;     		// 禁止DDR模式
	s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY; 		// DDR模式中数据延迟，这里用不到
	s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;			// 每次传输数据都发送指令	
	s_command.AddressMode 		 = QSPI_ADDRESS_1_LINE; 				// 1线地址模式
	s_command.DataMode    		 = QSPI_DATA_4_LINES;    				// 4线数据模式
	s_command.DummyCycles 		 = 0;                    				// 空周期个数
	s_command.NbData      		 = NumByteToWrite;      			   // 数据长度，最大只能256字节
	s_command.Address     		 = WriteAddr;         					// 要写入 W25Qxx 的地址
	s_command.Instruction 		 = W25Qxx_CMD_QuadInputPageProgram; // 1-1-4模式下(1线指令1线地址4线数据)，页编程指令
	
	// 写使能
	if (QSPI_W25Qxx_WriteEnable() != QSPI_W25Qxx_OK)
	{
		return W25Qxx_ERROR_WriteEnable;	// 写使能失败
	}
	// 写命令
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
	{
		return W25Qxx_ERROR_TRANSMIT;		// 传输数据错误
	}
	// 开始传输数据
	if (HAL_QSPI_Transmit(&hqspi, pBuffer, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
	{
		return W25Qxx_ERROR_TRANSMIT;		// 传输数据错误
	}
	// 使用自动轮询标志位，等待写入的结束 
	if (QSPI_W25Qxx_AutoPollingMemReady() != QSPI_W25Qxx_OK)
	{
		return W25Qxx_ERROR_AUTOPOLLING; // 轮询等待无响应
	}
	return QSPI_W25Qxx_OK;	// 写数据成功
}

/**
 * @brief 
 *	函数功能: 写入数据，最大不能超过flash芯片的大小，请务必完成擦除操作
 *	说    明: 	1.Flash的写入时间和擦除时间一样，是有限定的，并不是说QSPI驱动时钟133M就可以以这个速度进行写入
 *				2.按照 W25Q64JV 数据手册给出的 页 写入参考时间，典型值为 0.4ms，最大值为3ms
 *				3.实际的写入速度可能大于0.4ms，也可小于0.4ms
 *				4.Flash使用的时间越长，写入所需时间也会越长
 *				5.在数据写入之前，请务必完成擦除操作
 *				6.该函数移植于 stm32h743i_eval_qspi.c
 * 
 * @param pBuffer 			要写入的数据
 * @param WriteAddr 		要写入 W25Qxx 的地址
 * @param NumByteToWrite 	数据长度，最大不能超过flash芯片的大小
 * @return int8_t 			QSPI_W25Qxx_OK 		     - 读数据成功
 *				 			W25Qxx_ERROR_TRANSMIT	 - 传输失败
 *				 			W25Qxx_ERROR_AUTOPOLLING - 轮询等待无响应
 */
int8_t QSPI_W25Qxx_WriteBuffer(uint8_t* pBuffer, uint32_t WriteAddr, uint32_t NumByteToWrite)
{   
	int8_t status;

	// 计算需要擦除的扇区范围
	uint32_t start_sector = WriteAddr & ~(W25Qxx_SECTOR_SIZE - 1);
	uint32_t end_sector = (WriteAddr + NumByteToWrite - 1) & ~(W25Qxx_SECTOR_SIZE - 1);

	QSPI_W25Qxx_DBG("start_sector: 0x%x, WriteAddr: 0x%x", start_sector, WriteAddr);
	QSPI_W25Qxx_DBG("end_sector: 0x%x", end_sector);
	
	// 擦除所需的扇区
	for(uint32_t sector = start_sector; sector <= end_sector; sector += W25Qxx_SECTOR_SIZE) {
		QSPI_W25Qxx_DBG("Erasing sector at address 0x%X", (unsigned int)sector);
		if((status = QSPI_W25Qxx_SectorErase(sector)) != QSPI_W25Qxx_OK) {
			QSPI_W25Qxx_ERR("QSPI_W25Qxx_SectorErase failed, status: %d", status);
			return status;
		}
	}

	// 执行写入操作
	uint32_t current_addr = WriteAddr;
	uint32_t end_addr = WriteAddr + NumByteToWrite;
	uint32_t current_size;
	uint8_t* write_data = pBuffer;

	while(current_addr < end_addr) {
		current_size = W25Qxx_PageSize - (current_addr % W25Qxx_PageSize);
		if(current_size > (end_addr - current_addr)) {
			current_size = end_addr - current_addr;
		}

		// 写使能
		if (QSPI_W25Qxx_WriteEnable() != QSPI_W25Qxx_OK) {
			status = W25Qxx_ERROR_WriteEnable;
			QSPI_W25Qxx_ERR("QSPI_W25Qxx_WriteEnable failed, status: %d", status);
			return status;
		}

		// 写入数据
		if((status = QSPI_W25Qxx_WritePage(write_data, current_addr, current_size)) != QSPI_W25Qxx_OK) {
			QSPI_W25Qxx_ERR("QSPI_W25Qxx_WritePage failed, status: %d", status);
			return status;
		}

		current_addr += current_size;
		write_data += current_size;
	}

	status = QSPI_W25Qxx_OK;
	return status;
}

/**
 * @brief 
 * 函数功能: 读取数据，最大不能超过flash芯片的大小
 * 说    明: 1.Flash的读取速度取决于QSPI的通信时钟，最大不能超过133M
 *			2.这里使用的是1-4-4模式下(1线指令4线地址4线数据)，快速读取指令 Fast Read Quad I/O
 *			3.使用快速读取指令是有空周期的，具体参考W25Q64JV的手册  Fast Read Quad I/O  （0xEB）指令
 *			4.实际使用中，是否使用DMA、编译器的优化等以及数据存储区的位置(内部 TCM SRAM 或 AXI SRAM)都会影响读取的速度
 *			5.在本例程中，使用的是库函数进行直接读写，keil版本5.30，编译器AC6.14，编译等级Oz image size，读取速度为 7M字节/S ，数据放在 TCM SRAM 或者 AXI SRAM 都是差不多的结果
 *		    6.因为CPU直接访问外设寄存器的效率很低，直接使用HAL库进行读写的话，速度很慢，使用MDMA进行读取，可以达到 58M字节/S
 *	        7. W25Q64JV 所允许的最高驱动频率为133MHz，750的QSPI最高驱动频率也是133MHz ，但是对于HAL库函数直接读取而言，驱动时钟超过15M已经不会对性能有提升，对速度要求高的场合可以用MDMA的方式
 * @param pBuffer 			要读取的数据
 * @param ReadAddr 			要读取 W25Qxx 的地址
 * @param NumByteToRead 	数据长度，最大不能超过flash芯片的大小
 * @return int8_t 			QSPI_W25Qxx_OK 		     - 读数据成功
 *				 			W25Qxx_ERROR_TRANSMIT	 - 传输失败
 *				 			W25Qxx_ERROR_AUTOPOLLING - 轮询等待无响应
 */
int8_t QSPI_W25Qxx_ReadBuffer(uint8_t* pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead)
{
	int8_t status;

	QSPI_CommandTypeDef s_command;
	
	s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	s_command.AddressSize       = QSPI_ADDRESS_24_BITS;
	s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
	s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
	s_command.AddressMode       = QSPI_ADDRESS_4_LINES;
	s_command.DataMode          = QSPI_DATA_4_LINES;
	s_command.DummyCycles       = 6;
	s_command.NbData           = NumByteToRead;
	s_command.Address          = ReadAddr;
	s_command.Instruction      = W25Qxx_CMD_FastReadQuad_IO;
	
	if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
		status = W25Qxx_ERROR_TRANSMIT;
		return status;
	}

	if (HAL_QSPI_Receive(&hqspi, pBuffer, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK) {
		status = W25Qxx_ERROR_TRANSMIT;
		return status;
	}

	status = QSPI_W25Qxx_OK;
	return status;
}

int8_t QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(uint8_t* pData, uint32_t WriteAddr, uint32_t NumByteToWrite)
{
	bool is_xip = xip_enabled;
	if(is_xip == true) {
		QSPI_W25Qxx_ExitMemoryMappedMode();
	}

	bool result = QSPI_W25Qxx_WriteBuffer(pData, WriteAddr, NumByteToWrite);
	
	if(is_xip == true) {
		QSPI_W25Qxx_EnterMemoryMappedMode();
	}

	return result;
}

int8_t QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(uint8_t* pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead)
{
	bool is_xip = xip_enabled;
	if(is_xip == true) {
		QSPI_W25Qxx_ExitMemoryMappedMode();
	}

	bool result = QSPI_W25Qxx_ReadBuffer(pBuffer, ReadAddr, NumByteToRead);	

	if(is_xip == true) {	
		QSPI_W25Qxx_EnterMemoryMappedMode();
	}

	return result;
}

// 添加测试函数
int8_t QSPI_W25Qxx_Test(uint32_t test_addr)
{
	int8_t status;
	uint8_t write_buf[256];
	uint8_t read_buf[256];
	const uint32_t test_size = 256;
	
	QSPI_W25Qxx_DBG("Starting QSPI Flash R/W test...");
	
	// 准备测试数据
	for(uint32_t i = 0; i < test_size; i++) {
		write_buf[i] = i & 0xFF;
	}
	
	// 写入测试数据
	QSPI_W25Qxx_DBG("Writing test data to address 0x%08X...", (unsigned int)test_addr);
	status = QSPI_W25Qxx_WriteBuffer(write_buf, test_addr, test_size);
	if(status != QSPI_W25Qxx_OK) {
		QSPI_W25Qxx_ERR("Write test failed! Error code: %d", status);
		return status;
	}
	QSPI_W25Qxx_DBG("Write test data completed.");
	
	// 读取测试数据
	QSPI_W25Qxx_DBG("Reading test data from address 0x%08X...", (unsigned int)test_addr);
	status = QSPI_W25Qxx_ReadBuffer(read_buf, test_addr, test_size);
	if(status != QSPI_W25Qxx_OK) {
		QSPI_W25Qxx_ERR("Read test failed! Error code: %d", status);
		return status;
	}
	QSPI_W25Qxx_DBG("Read test data completed.");
	
	// 验证数据
	QSPI_W25Qxx_DBG("Verifying test data...");
	bool verify_ok = true;
	for(uint32_t i = 0; i < test_size; i++) {
		if(read_buf[i] != write_buf[i]) {
			QSPI_W25Qxx_ERR("Data mismatch at offset %d: wrote 0x%02X, read 0x%02X", 
				   (int)i, write_buf[i], read_buf[i]);
			verify_ok = false;
			break;
		}
	}
	
	if(verify_ok) {
		QSPI_W25Qxx_DBG("QSPI Flash R/W test passed!");
		return QSPI_W25Qxx_OK;
	} else {
		QSPI_W25Qxx_ERR("QSPI Flash R/W test failed!");
		return W25Qxx_ERROR_TRANSMIT;
	}
}

// 添加退出XIP模式的函数
int8_t QSPI_W25Qxx_ExitMemoryMappedMode(void)
{
	if(!xip_enabled) {
		return QSPI_W25Qxx_OK;
	}

	if(HAL_QSPI_Abort(&hqspi) != HAL_OK) {
		QSPI_W25Qxx_ERR("Exit XIP mode failed!");
		return W25Qxx_ERROR_MemoryMapped;
	}

	xip_enabled = false;
	QSPI_W25Qxx_DBG("Exit XIP mode success.");
	return QSPI_W25Qxx_OK;
}

/**
 * @brief 
 * 将QSPI设置为内存映射模式。
 * 设置为内存映射模式时，只能读，不能写！！！
 * 
 * @return int8_t 
 * QSPI_W25Qxx_OK - 写使能成功，W25Qxx_ERROR_WriteEnable - 写使能失败
 */
int8_t QSPI_W25Qxx_EnterMemoryMappedMode(void)
{
	if(xip_enabled) {
		return QSPI_W25Qxx_OK;
	}

	QSPI_CommandTypeDef      s_command;
	QSPI_MemoryMappedTypeDef s_mem_mapped_cfg;

	s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
	s_command.AddressSize       = QSPI_ADDRESS_24_BITS;
	s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
	s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
	s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
	s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
	s_command.AddressMode       = QSPI_ADDRESS_4_LINES;
	s_command.DataMode          = QSPI_DATA_4_LINES;
	s_command.DummyCycles       = 6;
	s_command.Instruction       = W25Qxx_CMD_FastReadQuad_IO;
	
	s_mem_mapped_cfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_DISABLE;
	s_mem_mapped_cfg.TimeOutPeriod     = 0;

	// 添加调试信息
	QSPI_W25Qxx_DBG("QSPI CR: 0x%08X", QUADSPI->CR);
	QSPI_W25Qxx_DBG("QSPI DCR: 0x%08X", QUADSPI->DCR);
	
	return HAL_QSPI_MemoryMapped(&hqspi, &s_command, &s_mem_mapped_cfg);
}

/**
 * @brief 
 * 判断是否处于内存映射模式。
 * 
 * @return bool 
 */
bool QSPI_W25Qxx_IsMemoryMappedMode(void)
{
	return xip_enabled;
}

//	实验平台：反客STM32H750XBH6核心板 （型号：FK750M4-XBH6）

/********************************************************************************************************************************************************************************************************FANKE**********/

int8_t QSPI_W25Qxx_WriteString(char* string, uint32_t ReadAddr)
{
	int8_t result;
	size_t size = strlen(string) + 1;
	uint8_t sizeBuffer[] = { (size>>24)&0xff, (size>>16)&0xff, (size>>8)&0xff, size&0xff };
	result = QSPI_W25Qxx_WriteBuffer(sizeBuffer, ReadAddr, sizeof(&sizeBuffer));	// 吸入len
	if(result != QSPI_W25Qxx_OK) {
		QSPI_W25Qxx_ERR("QSPI_W25Qxx_WriteString write size failure. error: %d", result);
		return result;
	} else {
		QSPI_W25Qxx_DBG("QSPI_W25Qxx_WriteString write size success. size: %d, 0x%02x%02x%02x%02x", size, sizeBuffer[0], sizeBuffer[1], sizeBuffer[2], sizeBuffer[3]);
	}
	result = QSPI_W25Qxx_WriteBuffer((uint8_t*)string, ReadAddr+sizeof(&sizeBuffer), size);
	if(result != QSPI_W25Qxx_OK) {
		QSPI_W25Qxx_ERR("QSPI_W25Qxx_WriteString write content failure. error: %d", result);
		return result;
	} else {
		return QSPI_W25Qxx_OK;
	}
}

int8_t QSPI_W25Qxx_ReadString(char* buffer, uint32_t ReadAddr)
{
	int8_t result;
	uint8_t sizeBuffer[4];
	uint32_t size;
	result = QSPI_W25Qxx_ReadBuffer(sizeBuffer, ReadAddr, sizeof(&sizeBuffer));
	if(result != QSPI_W25Qxx_OK) {
		QSPI_W25Qxx_ERR("QSPI_W25Qxx_ReadString read size failure. error: %d", result);
		return result;
	}
	
	size = ((uint32_t)sizeBuffer[0]<<24|(uint32_t)sizeBuffer[1]<<16|(uint32_t)sizeBuffer[2]<<8|(uint32_t)sizeBuffer[3]);

	// QSPI_W25Qxx_DBG("QSPI_W25Qxx_ReadString read size %x, %x, %x, %x, %x", (uint32_t)size, sizeBuffer[0], sizeBuffer[1], sizeBuffer[2], sizeBuffer[3]);

	QSPI_W25Qxx_DBG("QSPI_W25Qxx_ReadString read size: %d, 0x%08x, 0x%02x%02x%02x%02x", size, size, sizeBuffer[0], sizeBuffer[1], sizeBuffer[2], sizeBuffer[3]);

	if(size == 0xffffffff) {
		return -1;
	}

	result = QSPI_W25Qxx_ReadBuffer(buffer, ReadAddr + sizeof(uint32_t), size);
	if(result != QSPI_W25Qxx_OK) {
		QSPI_W25Qxx_ERR("QSPI_W25Qxx_ReadString read content failure. error: %d", result);
		return result;
	} else {
		return QSPI_W25Qxx_OK;
	}
}

/**
 * @brief  擦除指定地址范围的数据
 * @param  StartAddr 起始地址
 * @param  Size 要擦除的大小
 * @retval 0:正常
 *         -1:错误
 */
int8_t QSPI_W25Qxx_BufferErase(uint32_t StartAddr, uint32_t Size)
{
    int8_t result;
    uint32_t EndAddr;  // 结束地址
    uint32_t CurrentAddr;  // 当前地址

    // 计算结束地址
    EndAddr = StartAddr + Size - 1;

    // 检查地址范围
    if(EndAddr >= W25Qxx_FlashSize) {
        return W25Qxx_ERROR_TRANSMIT;
    }

    // 写使能
    result = QSPI_W25Qxx_WriteEnable();
    if(result != QSPI_W25Qxx_OK) {
        QSPI_W25Qxx_ERR("QSPI_W25Qxx_BufferErase write enable failure. error: %d", result);
        return result;
    }

    CurrentAddr = StartAddr;
    
    // 按照64K块、32K块和4K扇区依次擦除
    while(CurrentAddr <= EndAddr) {
        uint32_t remainSize = EndAddr - CurrentAddr + 1;

        // 如果剩余大小>=64K且地址对齐64K，使用64K块擦除
        if(remainSize >= 64*1024 && (CurrentAddr & (64*1024-1)) == 0) {
            result = QSPI_W25Qxx_BlockErase_64K(CurrentAddr);
            if(result != QSPI_W25Qxx_OK) {
                return result;
            }
            CurrentAddr += 64*1024;
        }
        // 如果剩余大小>=32K且地址对齐32K，使用32K块擦除
        else if(remainSize >= 32*1024 && (CurrentAddr & (32*1024-1)) == 0) {
            result = QSPI_W25Qxx_BlockErase_32K(CurrentAddr);
            if(result != QSPI_W25Qxx_OK) {
                return result;
            }
            CurrentAddr += 32*1024;
        }
        // 否则使用4K扇区擦除
        else {
            result = QSPI_W25Qxx_SectorErase(CurrentAddr);
            if(result != QSPI_W25Qxx_OK) {
                QSPI_W25Qxx_ERR("QSPI_W25Qxx_BufferErase sector erase failure. error: %d", result);
                return result;
            }
            CurrentAddr += 4*1024;
        }

        // 等待擦除完成
        result = QSPI_W25Qxx_AutoPollingMemReady();
        if(result != QSPI_W25Qxx_OK) {
            return result;
        }

        // 重新写使能
        result = QSPI_W25Qxx_WriteEnable();
        if(result != QSPI_W25Qxx_OK) {
            return result;
        }
    }

    result = QSPI_W25Qxx_OK;
	return result;
}


