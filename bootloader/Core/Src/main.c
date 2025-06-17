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
#include <stdio.h>
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
    BOOT_DBG("=== Starting Application Jump Process ===");
    
    // 进入内存映射模式
    if(QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK)
    {
        BOOT_ERR("QSPI_W25Qxx_EnterMemoryMappedMode failed");
        return;
    }
    BOOT_DBG("QSPI Flash memory mapped mode enabled");

    // 加载并验证元数据
    FirmwareMetadata metadata;
    int8_t load_result = DualSlot_LoadMetadata(&metadata);
    
    if (load_result != 0) {
        BOOT_ERR("Metadata loading failed: %d", load_result);
        BOOT_ERR("Using default slot A for startup");
        
        // 使用默认地址
        uint32_t app_base_address = DualSlot_GetSlotAddress("application", FIRMWARE_SLOT_A);
        if (app_base_address == 0) {
            BOOT_ERR("Cannot get application address for slot A");
            return;
        }
        
        // 验证槽位有效性
        if (!DualSlot_IsSlotValid(FIRMWARE_SLOT_A)) {
            BOOT_ERR("Slot A is invalid, cannot start");
            return;
        }
        
        BOOT_DBG("Using default address: 0x%08lX", (unsigned long)app_base_address);
        
        // 直接跳转到默认地址
        goto perform_jump;
    }
    
    // 打印元数据信息
    BOOT_DBG("=== Firmware Metadata Information ===");
    BOOT_DBG("Magic Number: 0x%08lX", (unsigned long)metadata.magic);
    BOOT_DBG("Metadata Version: %lu.%lu", (unsigned long)metadata.metadata_version_major, (unsigned long)metadata.metadata_version_minor);
    BOOT_DBG("Firmware Version: %s", metadata.firmware_version);
    BOOT_DBG("Target Slot: %s", (metadata.target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
    BOOT_DBG("Build Date: %s", metadata.build_date);
    BOOT_DBG("Device Model: %s", metadata.device_model);
    BOOT_DBG("Hardware Version: 0x%08lX", (unsigned long)metadata.hardware_version);
    BOOT_DBG("Component Count: %lu", (unsigned long)metadata.component_count);
    BOOT_DBG("CRC32: 0x%08lX", (unsigned long)metadata.metadata_crc32);
    
    // 打印组件信息
    for (uint32_t i = 0; i < metadata.component_count && i < FIRMWARE_COMPONENT_COUNT; i++) {
        const FirmwareComponent* comp = &metadata.components[i];
        BOOT_DBG("Component[%lu]: %s, Address=0x%08lX, Size=%lu, Active=%s", 
                 (unsigned long)i, comp->name, (unsigned long)comp->address, (unsigned long)comp->size, comp->active ? "Yes" : "No");
    }
    BOOT_DBG("=====================================");
    
    // 获取目标槽位
    FirmwareSlot target_slot = (FirmwareSlot)metadata.target_slot;
    BOOT_DBG("Target Slot: %s", (target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
    
    // 验证目标槽位有效性
    if (!DualSlot_IsSlotValid(target_slot)) {
        BOOT_ERR("Target slot %s is invalid, trying to switch to backup slot", 
                 (target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
        
        // 尝试使用另一个槽位
        FirmwareSlot backup_slot = (target_slot == FIRMWARE_SLOT_A) ? FIRMWARE_SLOT_B : FIRMWARE_SLOT_A;
        if (DualSlot_IsSlotValid(backup_slot)) {
            target_slot = backup_slot;
            BOOT_DBG("Switched to backup slot %s", (target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
        } else {
            BOOT_ERR("Backup slot %s is also invalid, cannot start", 
                     (backup_slot == FIRMWARE_SLOT_A) ? "A" : "B");
            return;
        }
    }
    
    // 获取应用程序地址
    uint32_t app_base_address = DualSlot_GetSlotAddress("application", target_slot);
    if (app_base_address == 0) {
        BOOT_ERR("Cannot get application address for slot %s", 
                 (target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
        return;
    }
    
    BOOT_DBG("Final slot %s, application base address: 0x%08lX", 
             (target_slot == FIRMWARE_SLOT_A) ? "A" : "B", (unsigned long)app_base_address);

perform_jump:
    // 读取向量表
    uint32_t app_stack = *(__IO uint32_t*)app_base_address;
    uint32_t jump_address = *(__IO uint32_t*)(app_base_address + 4);
    
    BOOT_DBG("Vector Table Information:");
    BOOT_DBG("  Stack Pointer (SP): 0x%08lX", (unsigned long)app_stack);
    BOOT_DBG("  Reset Vector (PC): 0x%08lX", (unsigned long)jump_address);

    // 验证栈指针有效性
    if ((app_stack & 0xFFF00000) != 0x20000000) {
        BOOT_ERR("Invalid stack pointer: 0x%08lX (should be in 0x20xxxxxx range)", (unsigned long)app_stack);
        return;
    }
    BOOT_DBG("Stack pointer validation passed");

    // 验证跳转地址有效性
    if ((jump_address & 0xFF000000) != 0x90000000) {
        BOOT_ERR("Invalid jump address: 0x%08lX (should be in 0x90xxxxxx range)", (unsigned long)jump_address);
        return;
    }
    BOOT_DBG("Jump address validation passed");

    // 检查目标代码的前几条指令
    uint16_t* code_ptr = (uint16_t*)(jump_address & ~1UL);
    BOOT_DBG("First 4 instructions at target address:");
    for(int i = 0; i < 4; i++) {
        BOOT_DBG("  [%d]: 0x%04X", i, code_ptr[i]);
    }

    // 验证是否为有效的ARM Thumb指令
    uint16_t first_instruction = code_ptr[0];
    if (first_instruction == 0x0000 || first_instruction == 0xFFFF) {
        BOOT_ERR("Target address contains invalid instruction: 0x%04X", first_instruction);
        return;
    }
    BOOT_DBG("Target code validation passed");

    /****************************  跳转前准备  ************************* */
    BOOT_DBG("Starting pre-jump preparation...");
    
    // 关闭SysTick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    BOOT_DBG("SysTick disabled");

    // 设置特权模式并禁用中断
    __set_CONTROL(0);
    __disable_irq();
    __set_PRIMASK(1);
    BOOT_DBG("Interrupts disabled, entered privileged mode");

    // 清除所有中断
    for(int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    BOOT_DBG("NVIC interrupts cleared");

    // 禁用MPU
    HAL_MPU_Disable();
    BOOT_DBG("MPU disabled");

    // 设置向量表
    SCB->VTOR = app_base_address;
    BOOT_DBG("Vector table base address set to: 0x%08lX", (unsigned long)SCB->VTOR);
    
    // 验证向量表设置
    if (SCB->VTOR != app_base_address) {
        BOOT_ERR("Vector table setup failed: Expected=0x%08lX, Actual=0x%08lX", 
                 (unsigned long)app_base_address, (unsigned long)SCB->VTOR);
        return;
    }

    // 设置主堆栈指针
    __set_MSP(app_stack);
    uint32_t current_msp = __get_MSP();
    if (current_msp != app_stack) {
        BOOT_ERR("Stack pointer setup failed: Expected=0x%08lX, Actual=0x%08lX", 
                 (unsigned long)app_stack, (unsigned long)current_msp);
        return;
    }
    BOOT_DBG("Main stack pointer set to: 0x%08lX", (unsigned long)current_msp);

    // 内存屏障确保所有操作完成
    __DSB();
    __ISB();
    BOOT_DBG("Memory barriers executed");

    // 确保跳转地址包含Thumb位
    jump_address |= 0x1;
    BOOT_DBG("Final jump address (with Thumb bit): 0x%08lX", (unsigned long)jump_address);

    // 创建函数指针
    typedef void (*pFunction)(void);
    pFunction app_reset_handler = (pFunction)jump_address;

    BOOT_DBG("=== Ready to Jump to Application ===");
    BOOT_DBG("Jumping now...");
    
    // 最后一次内存屏障
    __DSB();
    __ISB();
    
    // 跳转到应用程序
    app_reset_handler();

    // 不应该到达这里
    BOOT_ERR("Jump failed! Program should not return here");
    while(1) {
        // 无限循环，表示跳转失败
    }
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
