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



/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    // board_init();
    // 使能中断
    __enable_irq(); 

    HAL_Init();

    // printf("main\r\n");
    /* USER CODE BEGIN 1 */
    HAL_Delay(1000);
    UserLEDClose();

    QSPI_W25Qxx_ExitMemoryMappedMode();

    USART1_Init();
    APP_DBG("USART1_Init success\r\n");
    /* 测试各个段 */
    APP_DBG("rodata_test (should be 0x12345678): 0x%08X", rodata_test);
    APP_DBG("data_test (should be 0x87654321): 0x%08X", data_test);
    APP_DBG("bss_test (should be 0): 0x%08X", bss_test);
    
    // // 修改并再次检查
    data_test = 0x11223344;
    bss_test = 0x44332211;
    APP_DBG("data_test after modify: 0x%08X", data_test);
    APP_DBG("bss_test after modify: 0x%08X", bss_test);
    
    QSPI_W25Qxx_ReadBuffer((uint8_t *)data_test, 0x00000000, 4);
    APP_DBG("data_test after read: 0x%08X", data_test);
    

    while (1)
    {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}



/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

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
    /* USER CODE END Error_Handler_Debug */
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
