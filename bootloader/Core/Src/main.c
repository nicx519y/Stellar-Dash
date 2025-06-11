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
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usart.h"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include "dual_slot_config.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
HCD_HandleTypeDef hhcd_USB_OTG_HS;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MPU_Config(void);
void JumpToApplication(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  MPU_Config();
  HAL_Init();
  USART1_Init();
  
  // 初始化QSPI Flash
  if(QSPI_W25Qxx_Init() != QSPI_W25Qxx_OK) {
    BOOT_ERR("QSPI_W25Qxx_Init failed\r\n");
    return -1;
  }
  BOOT_DBG("QSPI_W25Qxx_Init success\r\n");

  // 初始化双槽升级系统
  if(DualSlot_Init() != 0) {
    BOOT_ERR("DualSlot_Init failed, using legacy mode\r\n");
  }

  // 双槽测试功能（仅用于开发测试）
#if DUAL_SLOT_TEST_ENABLE
  if(DualSlot_IsEnabled()) {
    BOOT_DBG("=== Dual Slot Test Mode Enabled ===");
    
#if DUAL_SLOT_FORCE_SLOT_A
    BOOT_DBG("Force switching to Slot A");
    if(DualSlot_SetCurrentSlot(SLOT_A) == 0) {
      BOOT_DBG("Successfully switched to Slot A");
    } else {
      BOOT_ERR("Failed to switch to Slot A");
    }
#elif DUAL_SLOT_FORCE_SLOT_B
    BOOT_DBG("Force switching to Slot B");
    if(DualSlot_SetCurrentSlot(SLOT_B) == 0) {
      BOOT_DBG("Successfully switched to Slot B");
    } else {
      BOOT_ERR("Failed to switch to Slot B");
    }
#endif

    // 输出槽信息
    slot_info_t slot_info_a, slot_info_b;
    if(DualSlot_GetSlotInfo(SLOT_A, &slot_info_a) == 0) {
      BOOT_DBG("Slot A Info:");
      BOOT_DBG("  Base: 0x%08X, App: 0x%08X, Size: %d KB", 
               slot_info_a.base_address, 
               slot_info_a.application_address,
               slot_info_a.application_size / 1024);
      BOOT_DBG("  WebRes: 0x%08X, ADC: 0x%08X", 
               slot_info_a.webresources_address,
               slot_info_a.adc_mapping_address);
    }
    
    if(DualSlot_GetSlotInfo(SLOT_B, &slot_info_b) == 0) {
      BOOT_DBG("Slot B Info:");
      BOOT_DBG("  Base: 0x%08X, App: 0x%08X, Size: %d KB", 
               slot_info_b.base_address, 
               slot_info_b.application_address,
               slot_info_b.application_size / 1024);
      BOOT_DBG("  WebRes: 0x%08X, ADC: 0x%08X", 
               slot_info_b.webresources_address,
               slot_info_b.adc_mapping_address);
    }
    
    BOOT_DBG("=== End of Dual Slot Test ===");
  }
#endif

  // 输出当前配置信息
  if(DualSlot_IsEnabled()) {
    uint8_t current_slot = DualSlot_GetCurrentSlot();
    uint32_t app_address = DualSlot_GetSlotApplicationAddress(current_slot);
    BOOT_DBG("Dual slot enabled - Current: Slot %c, Address: 0x%08X", 
             (current_slot == SLOT_A) ? 'A' : 'B', app_address);
  } else {
    BOOT_DBG("Dual slot disabled - Legacy mode, Address: 0x%08X", W25Qxx_Mem_Addr);
  }

  // QSPI_W25Qxx_Test(0x00500000);

  JumpToApplication();

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 80;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInitStruct.PLL3.PLL3M = 2;
  PeriphClkInitStruct.PLL3.PLL3N = 15;
  PeriphClkInitStruct.PLL3.PLL3P = 2;
  PeriphClkInitStruct.PLL3.PLL3Q = 4;
  PeriphClkInitStruct.PLL3.PLL3R = 5;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_3;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL3;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* 配置 QSPI Flash 区域 */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x90000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_8MB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;  // 必须显式允许执行
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}



void JumpToApplication(void)
{
    // 进入内存映射模式
    if(QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK)
    {
      BOOT_ERR("QSPI_W25Qxx_EnterMemoryMappedMode failed\r\n");
    }

    BOOT_DBG("QSPI_W25Qxx_EnterMemoryMappedMode success\r\n");

    // 获取应用程序地址（支持双槽或单槽模式）
    uint32_t app_base_address;
    if(DualSlot_IsEnabled()) {
        // 双槽模式：获取当前槽的地址
        uint8_t current_slot = DualSlot_GetCurrentSlot();
        app_base_address = DualSlot_GetSlotApplicationAddress(current_slot);
        BOOT_DBG("Dual slot mode: Using Slot %c", (current_slot == SLOT_A) ? 'A' : 'B');
        
        // 验证当前槽的有效性
        if(DualSlot_ValidateSlot(current_slot) != 0) {
            BOOT_ERR("Current slot %c is invalid, trying to switch", 
                     (current_slot == SLOT_A) ? 'A' : 'B');
            
            // 尝试切换到另一个槽
            if(DualSlot_SwitchSlot() == 0) {
                current_slot = DualSlot_GetCurrentSlot();
                app_base_address = DualSlot_GetSlotApplicationAddress(current_slot);
                BOOT_DBG("Switched to Slot %c", (current_slot == SLOT_A) ? 'A' : 'B');
            } else {
                BOOT_ERR("Failed to switch slot, using current address anyway");
            }
        }
    } else {
        // 单槽模式：使用兼容地址（原来的逻辑）
        app_base_address = DualSlot_GetLegacyApplicationAddress();
        BOOT_DBG("Legacy single slot mode");
    }

    uint32_t jump_address = *(__IO uint32_t*)(app_base_address + 4);
    uint32_t app_stack = *(__IO uint32_t*)app_base_address;
    
    BOOT_DBG("App Stack address: 0x%08X, App Stack value: 0x%08X", app_base_address, *(__IO uint32_t*)app_base_address);
    BOOT_DBG("Jump Address: 0x%08X, Jump Address value: 0x%08X", app_base_address + 4, *(__IO uint32_t*)(app_base_address + 4));

    // 验证栈指针和跳转地址
    if ((app_stack & 0xFF000000) != 0x20000000) {
        BOOT_ERR("Invalid stack pointer: 0x%08X", app_stack);
        return;
    } else {
        BOOT_DBG("Valid stack pointer: 0x%08X", app_stack);
    }

    if ((jump_address & 0xFF000000) != 0x90000000) {
        BOOT_ERR("Invalid jump address: 0x%08X", jump_address);
        return;
    } else {
        BOOT_DBG("Valid jump address: 0x%08X", jump_address);
    }

    // 先验证一下目标地址的内容
    uint16_t* code_ptr = (uint16_t*)(jump_address & ~1UL);
    BOOT_DBG("First instructions at target:");
    for(int i = 0; i < 4; i++) {
        BOOT_DBG("  Instruction %d: 0x%04X", i, code_ptr[i]);
    }


    /****************************  跳转前准备  ************************* */
    SysTick->CTRL = 0;		                        // 关闭SysTick
    SysTick->LOAD = 0;		                        // 清零重载
    SysTick->VAL = 0;			                        // 清零计数

    __set_CONTROL(0); //priviage mode 
    __disable_irq(); //disable interrupt
    __set_PRIMASK(1);

    /* 禁用缓存 */
    // SCB_DisableICache();
    // SCB_DisableDCache();

    // 关闭所有中断
    __disable_irq();
    BOOT_DBG("Interrupts disabled");

    // 清除所有中断
    for(int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    BOOT_DBG("NVIC cleared");

    HAL_MPU_Disable();

    // 设置向量表
    SCB->VTOR = app_base_address;
    BOOT_DBG("VTOR set to: 0x%08X", SCB->VTOR);
    
    // 验证向量表设置是否生效
    BOOT_DBG("SCB->VTOR after set: 0x%08X", SCB->VTOR);
    BOOT_DBG("Stack Pointer from vector: 0x%08X", *(__IO uint32_t*)SCB->VTOR);
    BOOT_DBG("Reset Handler from vector: 0x%08X", *(__IO uint32_t*)(SCB->VTOR + 4));

    // 设置主堆栈指针
    __set_MSP(app_stack);
    BOOT_DBG("MSP set to: 0x%08X", __get_MSP());

    // 清除缓存
    // SCB_CleanInvalidateDCache();
    // SCB_InvalidateICache();
    // BOOT_DBG("Cache cleared");

    // 内存屏障
    __DSB();
    __ISB();
    BOOT_DBG("Memory barriers executed");

    // 确保跳转地址是 Thumb 模式
    jump_address |= 0x1;
    BOOT_DBG("Final jump address (with Thumb bit): 0x%08X", jump_address);

    // 使用函数指针跳转
    typedef void (*pFunction)(void);
    pFunction app_reset_handler = (pFunction)jump_address;

    BOOT_DBG("About to jump...");
    BOOT_DBG("Last debug message before jump!");
    
    // 最后一次内存屏障
    __DSB();
    __ISB();
    
    // 跳转
    app_reset_handler();

    // 不应该到达这里
    BOOT_ERR("Jump failed!");
    while(1);
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
