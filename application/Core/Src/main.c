/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "main.h"
#include "usart.h"
#include "board_cfg.h"
#include "qspi-w25q64.h"
#include "bsp/board_api.h"


HCD_HandleTypeDef hhcd_USB_OTG_HS;

/* 测试各个段 */
const uint32_t rodata_test = 0x12345678;        // .rodata 段
uint32_t data_test = 0x87654321;                // .data 段
uint32_t bss_test;                              // .bss 段


void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
void UserLEDClose(void);
void dataSectionTest(void);
void enableFPU(void);
void floatTest(void);


/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    // 使能中断
    __enable_irq(); 
    enableFPU(); // 使能FPU
    HAL_Init();
    HAL_Delay(200); // 延时200ms 等待时钟稳定，并且验证时钟配置是否正确 中断是否可用
    UserLEDClose(); // 关闭LED 表示已经进入main函数
    QSPI_W25Qxx_ExitMemoryMappedMode(); // 退出内存映射模式，qspi-flash可以正常读写
    board_init(); // 初始化板子 时钟 串口 WS2812B 等
    dataSectionTest(); // 测试各个段，测试堆内存
    floatTest(); // 测试FPU 是否能打印浮点数

    while (1);
}


/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    printf("Error_Handler...\r\n");
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
}

void UserLEDClose(void)
{
    // 使能 GPIO 时钟
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
    // 等待时钟稳定
    __DSB();
    // 设置 PC13 为输出模式
    GPIOC->MODER &= ~(3U << (13 * 2));  // 清除原来的模式位
    GPIOC->MODER |= (1U << (13 * 2));   // 设置为输出模式
    // 设置为推挽输出
    GPIOC->OTYPER &= ~(1U << 13);
    // 设置为低速
    GPIOC->OSPEEDR &= ~(3U << (13 * 2));
    // 设置为无上拉下拉
    GPIOC->PUPDR &= ~(3U << (13 * 2));
    // 直接输出低电平 (LED 灭)
    GPIOC->BSRR = (1U << 13); 
}

void dataSectionTest(void)
{
     /* 测试各个段 */
    APP_DBG("rodata_test (should be 0x12345678): 0x%08X", rodata_test);
    APP_DBG("data_test (should be 0x87654321): 0x%08X", data_test);
    APP_DBG("bss_test (should be 0): 0x%08X", bss_test);
    
    // // 修改并再次检查
    data_test = 0x11223344;
    bss_test = 0x44332211;
    APP_DBG("data_test after modify: 0x%08X", data_test);
    APP_DBG("bss_test after modify: 0x%08X", bss_test);
    
    // 测试flash读
    QSPI_W25Qxx_ReadBuffer((uint8_t *)data_test, 0x00000000, 4);
    APP_DBG("data_test after read: 0x%08X", data_test);

    // 测试堆内存
    APP_DBG("Testing heap memory...");
    
    // 测试小块分配
    uint32_t* p1 = malloc(4);
    uint32_t* p2 = malloc(4);
    if (p1 && p2) {
        APP_DBG("Small allocations: p1 = 0x%p, p2 = 0x%p", (void*)p1, (void*)p2);
        APP_DBG("Distance between allocations: %d bytes", 
                (uint8_t*)p2 - (uint8_t*)p1);
    }
    free(p1);
    free(p2);

    // 测试渐增大小的分配
    for (size_t size = 4; size <= 256; size *= 2) {
        void* p = malloc(size);
        if (p) {
            APP_DBG("Allocated %u bytes at 0x%p", size, p);
            free(p);
        } else {
            APP_ERR("Failed to allocate %u bytes", size);
            break;
        }
    }

    // 测试最大连续分配
    size_t size = 4;
    void* p = malloc(size);
    while (p != NULL) {
        free(p);
        size *= 2;
        p = malloc(size);
    }
    APP_DBG("Maximum single allocation: %u bytes", size/2);
}

void enableFPU(void)
{
    #if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
        // 启用 CP10 和 CP11 完全访问
        SCB->CPACR |= ((3UL << 10*2)|(3UL << 11*2));
        
        // 设置 FPSCR
        uint32_t fpscr = __get_FPSCR();
        // 设置舍入模式为 Round to Nearest (RN)
        fpscr &= ~FPU_FPDSCR_RMode_Msk;  // 清除舍入模式位 [23:22]
        fpscr |= FPU_FPDSCR_RMode_RN;   // 设置为 RN 模式 (0b00)
        __set_FPSCR(fpscr);
        
        __DSB();
        __ISB();
        
    #endif
}

void floatTest(void)
{
    APP_DBG("FPU test start...");
    
    // 检查 CPACR
    uint32_t cpacr = SCB->CPACR;
    APP_DBG("CPACR = 0x%08lX", cpacr);
    
    // 检查 FPSCR
    uint32_t fpscr = __get_FPSCR();
    APP_DBG("FPSCR = 0x%08lX", fpscr);

    // 简单的浮点运算测试
    volatile float a = 1.0f;
    volatile float b = 2.0f;
    volatile float c = a + b;

    APP_DBG("a = %f, b = %f, c = %f", a, b, c);

}
#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
