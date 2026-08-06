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
#include <stdarg.h>
#include "usart.h"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include "dual_slot_config.h"
#include "boot_attestation.h"
#include "firmware_security.h"
#include "secure_access_handoff.h"
#include "system_logger.h"  // 日志模块头文件
#include "gpio.h"
#if defined(HBOX_FACTORY_IDENTITY_ENROLLMENT) && \
    HBOX_FACTORY_IDENTITY_ENROLLMENT
#include "factory_identity_service.h"
#endif
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
  // 先初始化基础硬件，但不初始化日志系统
  MPU_Config();
  HAL_Init();
  Board_InitSafePowerState();
  /* 3V3_Main powers the external W25Q64; wait before its first QSPI access. */
  HAL_Delay(MAIN_POWER_STABILIZE_MS);
  SystemClock_Config();
  PeriphCommonClock_Config();
  USART1_Init();

  BOOT_STAGE("B01", "reset entry; MPU, HAL, safe power, clocks and USART1 ready");
  BOOT_STAGE("B02", "main power stabilized for %lu ms; LCD_EN held high",
             (unsigned long)MAIN_POWER_STABILIZE_MS);
  
  // 使用BOOT_DBG输出初始化状态（不依赖日志系统）
  BOOT_DBG("HBox Bootloader v2.0.0 starting...");
  BOOT_DBG("MPU/HAL/USART1 initialized");
  
  // 初始化QSPI Flash（必须在日志系统之前）
  BOOT_STAGE("B03", "QSPI initialization begin");
  if(QSPI_W25Qxx_Init() != QSPI_W25Qxx_OK) {
    BOOT_STAGE_ERROR("B03", "QSPI initialization failed; boot stopped");
    BOOT_ERR("QSPI_W25Qxx_Init failed\r\n");
    return -1;
  }
  BOOT_STAGE("B03", "QSPI initialization complete");
  
  // 等待一小段时间确保QSPI完全初始化
  HAL_Delay(50);
  
  // 现在可以安全地初始化日志模块
  BOOT_STAGE("B04", "persistent logger initialization begin");
  LogResult init_result = Logger_Init(true, LOG_LEVEL_DEBUG);

  if (init_result != LOG_RESULT_SUCCESS) {
    BOOT_STAGE_ERROR("B04", "persistent logger initialization failed: %d",
                     init_result);
    BOOT_ERR("Logger_Init failed with error: %d", init_result);
    return -1;
  }
  BOOT_STAGE("B04", "persistent logger initialization complete");

  // QSPI_W25Qxx_Test(0x00500000);

  Logger_Log(LOG_LEVEL_INFO, "BOOTLOADER", "System startup - MPU/HAL/USART1 initialized");

#if defined(HBOX_FACTORY_IDENTITY_ENROLLMENT) && \
    HBOX_FACTORY_IDENTITY_ENROLLMENT
  /*
   * Factory-only builds have no implicit transport or authorization bypass.
   * The explicitly injected service receives the retained enrollment API and
   * its independent gate source must authorize every internal-Flash write.
   */
  HBoxFactoryIdentityService_Run(
      HBoxFactoryIdentityEnrollment_GetApi());
#endif

  BOOT_STAGE("B05", "secure application handoff begin");
  JumpToApplication();
  BOOT_STAGE_ERROR("B05", "application handoff returned without transfer");

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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
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
  PeriphClkInitStruct.PLL3.PLL3M = 5;
  PeriphClkInitStruct.PLL3.PLL3N = 36;
  PeriphClkInitStruct.PLL3.PLL3P = 2;
  PeriphClkInitStruct.PLL3.PLL3Q = 4;
  PeriphClkInitStruct.PLL3.PLL3R = 4;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
  PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL3;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

// BOOT_DBG包装函数，用于日志打印
static int boot_debug_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buffer[512];  // 临时缓冲区
    int ret = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    BOOT_DBG("%s", buffer);  // 使用BOOT_DBG输出
    return ret;
}

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
  Logger_Log(LOG_LEVEL_FATAL, "HAL", "HAL Error Handler called - system halted");
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}



void JumpToApplication(void)
{
    BootAttestation_Invalidate();
    BOOT_STAGE("B06", "previous boot attestation context invalidated");


    // 进入内存映射模式
    if(QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK)
    {
        BOOT_STAGE_ERROR("B07", "QSPI memory-mapped mode entry failed");
        Logger_Log(LOG_LEVEL_ERROR, "QSPI", "Failed to enter memory mapped mode");
        BOOT_ERR("QSPI_W25Qxx_EnterMemoryMappedMode failed");
        return;
    }
    BOOT_STAGE("B07", "QSPI memory-mapped mode active");

    

    // 加载并验证元数据
    FirmwareMetadata metadata;
    int8_t load_result = DualSlot_LoadMetadata(&metadata);
    uint32_t app_base_address;
    FirmwareSlot target_slot;
    
    if (load_result != 0) {
        BOOT_STAGE_ERROR("B08", "signed metadata rejected: code=%d",
                         load_result);
#if HBOX_SECURE_BOOT_REQUIRED
        Logger_Log(LOG_LEVEL_ERROR, "METADATA",
                   "Signed metadata validation failed (code=%d); entering recovery",
                   load_result);
        BOOT_ERR("Secure metadata validation failed; refusing unsigned fallback");
        return;
#else
        Logger_Log(LOG_LEVEL_WARN, "METADATA", "Metadata load failed (code=%d), using default slot A", load_result);
        
        // 使用默认地址
        app_base_address = DualSlot_GetSlotAddress("application", FIRMWARE_SLOT_A);
        if (app_base_address == 0) {
            Logger_Log(LOG_LEVEL_ERROR, "SLOT", "Cannot get slot A address");
            BOOT_ERR("Cannot get application address for slot A");
            return;
        }
        
        // 验证槽位有效性
        if (!DualSlot_IsSlotValid(FIRMWARE_SLOT_A)) {
            Logger_Log(LOG_LEVEL_ERROR, "SLOT", "Slot A invalid, cannot start application");
            BOOT_ERR("Slot A is invalid, cannot start");
            return;
        }
        
        target_slot = FIRMWARE_SLOT_A;
        goto perform_jump;
#endif
    }
    
    // 元数据加载成功
    target_slot = (FirmwareSlot)metadata.target_slot;
    BOOT_STAGE("B08", "signed metadata accepted: target slot=%s, version=%s",
               (target_slot == FIRMWARE_SLOT_A) ? "A" : "B",
               metadata.firmware_version);
    Logger_Log(LOG_LEVEL_INFO, "METADATA", "Loaded metadata: FW=%s, Target=%s, Build=%s", 
                     metadata.firmware_version, 
                     (target_slot == FIRMWARE_SLOT_A) ? "A" : "B",
                     metadata.build_date);
    
    // 验证目标槽位有效性
    if (!DualSlot_IsSlotValid(target_slot)) {
        BOOT_STAGE_ERROR("B09", "target slot %s validation failed",
                         (target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
#if HBOX_SECURE_BOOT_REQUIRED
        Logger_Log(LOG_LEVEL_ERROR, "SLOT",
                   "Signed target slot invalid; entering controlled recovery");
        BOOT_ERR("Signed target slot is invalid; refusing unauthenticated fallback");
        return;
#else
        FirmwareSlot backup_slot = (target_slot == FIRMWARE_SLOT_A) ? FIRMWARE_SLOT_B : FIRMWARE_SLOT_A;
        Logger_Log(LOG_LEVEL_WARN, "SLOT", "Target slot %s invalid, trying backup slot %s", 
                         (target_slot == FIRMWARE_SLOT_A) ? "A" : "B",
                         (backup_slot == FIRMWARE_SLOT_A) ? "A" : "B");
        
        if (DualSlot_IsSlotValid(backup_slot)) {
            target_slot = backup_slot;
        } else {
            Logger_Log(LOG_LEVEL_ERROR, "SLOT", "Both slots invalid, cannot start");
            BOOT_ERR("Backup slot %s is also invalid, cannot start", 
                     (backup_slot == FIRMWARE_SLOT_A) ? "A" : "B");
            return;
        }
#endif
    }
    BOOT_STAGE("B09", "target slot %s image and signature accepted",
               (target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
    
    // 获取应用程序地址
    app_base_address = DualSlot_GetSlotAddress("application", target_slot);
    if (app_base_address == 0) {
        BOOT_STAGE_ERROR("B10", "application address lookup failed for slot %s",
                         (target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
        Logger_Log(LOG_LEVEL_ERROR, "SLOT", "Cannot get address for slot %s", 
                         (target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
        BOOT_ERR("Cannot get application address for slot %s", 
                 (target_slot == FIRMWARE_SLOT_A) ? "A" : "B");
        return;
    }

perform_jump:
    // 读取向量表
    uint32_t app_stack = *(__IO uint32_t*)app_base_address;
    uint32_t jump_address = *(__IO uint32_t*)(app_base_address + 4);
    
    // 验证向量表有效性
    if ((app_stack & 0xFFF00000) != 0x20000000 || (jump_address & 0xFF000000) != 0x90000000) {
        BOOT_STAGE_ERROR("B10", "vector table rejected: SP=0x%08lX PC=0x%08lX",
                         (unsigned long)app_stack,
                         (unsigned long)jump_address);
        Logger_Log(LOG_LEVEL_ERROR, "VECTOR", "Invalid vector table: SP=0x%08lX, PC=0x%08lX", 
                         (unsigned long)app_stack, (unsigned long)jump_address);
        BOOT_ERR("Invalid vector table addresses");
        return;
    }

    // 验证目标代码有效性
    uint16_t* code_ptr = (uint16_t*)(jump_address & ~1UL);
    uint16_t first_instruction = code_ptr[0];
    if (first_instruction == 0x0000 || first_instruction == 0xFFFF) {
        BOOT_STAGE_ERROR("B10", "reset handler instruction rejected: 0x%04X",
                         first_instruction);
        Logger_Log(LOG_LEVEL_ERROR, "CODE", "Invalid instruction at target: 0x%04X", first_instruction);
        BOOT_ERR("Target address contains invalid instruction: 0x%04X", first_instruction);
        return;
    }
    BOOT_STAGE("B10", "vector table accepted: base=0x%08lX SP=0x%08lX PC=0x%08lX",
               (unsigned long)app_base_address,
               (unsigned long)app_stack,
               (unsigned long)jump_address);
#if HBOX_SECURE_BOOT_REQUIRED
    hbox_secure_access_status_t secure_access_status =
        HBoxSecureAccess_ValidateLifecycle();
    if (secure_access_status == HBOX_SECURE_ACCESS_AREA_MISMATCH &&
        HBoxSecureAccess_CanInitializeFullInternalFlashArea()) {
        BOOT_STAGE("B11", "blank SCAR detected under SECURITY/RDP1; RSS full internal secure-area initialization begin");
        Logger_Log(LOG_LEVEL_WARN, "SECURE_ACCESS",
                   "Initializing the one-time full internal Flash secure area through RSS");
        Logger_Flush();
        HBoxSecureAccess_InitializeFullInternalFlashArea();
    }
    if (secure_access_status != HBOX_SECURE_ACCESS_OK) {
        BOOT_STAGE_ERROR("B11", "secure lifecycle rejected: %s (%d)",
                         HBoxSecureAccess_StatusString(secure_access_status),
                         (int)secure_access_status);
        BootAttestation_Invalidate();
        Logger_Log(LOG_LEVEL_ERROR, "SECURE_ACCESS",
                   "Lifecycle validation failed: %s (%d)",
                   HBoxSecureAccess_StatusString(secure_access_status),
                   (int)secure_access_status);
        BOOT_ERR("Secure user area is not ready; refusing application handoff");
        return;
    }
    BOOT_STAGE("B11", "secure lifecycle accepted");

    /*
     * The complete target slot and its release signature were accepted above.
     * Commit the new minimum before transferring execution so that a reset
     * cannot re-open an older signed image. Missing/unprovisioned/corrupt
     * monotonic storage is a recovery condition, never a reason to continue.
     */
    if (!FirmwareSecurity_CommitValidatedSecurityVersion(&metadata)) {
        hbox_security_version_status_t version_status =
            FirmwareSecurity_LastSecurityVersionStatus();
        BOOT_STAGE_ERROR("B12", "anti-rollback commit failed: %s (%d)",
                         HBoxSecurityVersion_StatusString(version_status),
                         (int)version_status);
        Logger_Log(LOG_LEVEL_ERROR, "ANTIROLLBACK",
                   "Minimum security-version commit failed: %s (%d)",
                   HBoxSecurityVersion_StatusString(version_status),
                   (int)version_status);
        BOOT_ERR("Anti-rollback provider unavailable or invalid; refusing jump");
        return;
    }
    BOOT_STAGE("B12", "anti-rollback minimum committed");

    /*
     * No identity, invalid manufacturer certificate, entropy failure, or
     * signing failure means no application handoff.  This is the device-side
     * root for the later WebHID online proof.
     */
    if (!BootAttestation_Prepare(&metadata)) {
        BOOT_STAGE_ERROR("B13", "device identity or boot attestation unavailable");
        Logger_Log(LOG_LEVEL_ERROR, "ATTESTATION",
                   "Unable to create an authenticated boot context");
        BOOT_ERR("Device identity/boot attestation unavailable; refusing jump");
        return;
    }
    BOOT_STAGE("B13", "authenticated boot context prepared");
#endif

    BOOT_DBG("Jumping to slot %s: Base=0x%08lX, SP=0x%08lX, PC=0x%08lX", 
                     (target_slot == FIRMWARE_SLOT_A) ? "A" : "B",
                     (unsigned long)app_base_address,
                     (unsigned long)app_stack, 
                     (unsigned long)jump_address);

    Logger_Log(LOG_LEVEL_INFO, "JUMP", "Jumping to slot %s: Base=0x%08lX, SP=0x%08lX, PC=0x%08lX", 
                     (target_slot == FIRMWARE_SLOT_A) ? "A" : "B",
                     (unsigned long)app_base_address,
                     (unsigned long)app_stack, 
                     (unsigned long)jump_address);

    
#if HBOX_SECURE_BOOT_REQUIRED
    BOOT_STAGE("B14", "ROM secure-area transfer selected; flushing logs");
#else
    BOOT_STAGE("B14", "unlocked development direct transfer selected; flushing logs");
#endif
    // 刷新日志缓冲区，确保日志写入Flash，再往下无法使用logger
    Logger_Flush();
    BOOT_STAGE("B15", "logs flushed; teardown begins; next milestone must be APP A01");

    /****************************  跳转前准备  ************************* */
    // 关闭SysTick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    // 设置特权模式并禁用中断
    __set_CONTROL(0);
    __disable_irq();
    __set_PRIMASK(1);

    // 清除所有中断
    for(int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    

    // 禁用MPU
    HAL_MPU_Disable();

    // 设置向量表
    SCB->VTOR = app_base_address;
    
    // 验证向量表设置
    if (SCB->VTOR != app_base_address) {
        BOOT_STAGE_ERROR("B16", "VTOR update failed: expected=0x%08lX actual=0x%08lX",
                         (unsigned long)app_base_address,
                         (unsigned long)SCB->VTOR);
        BOOT_ERR("Vector table setup failed: Expected=0x%08lX, Actual=0x%08lX", 
                 (unsigned long)app_base_address, (unsigned long)SCB->VTOR);
        Logger_Log(LOG_LEVEL_ERROR, "VECTOR", "Vector table setup failed: Expected=0x%08lX, Actual=0x%08lX", 
                         (unsigned long)app_base_address, (unsigned long)SCB->VTOR);
        return;
    }

#if HBOX_SECURE_BOOT_REQUIRED
    /*
     * Do not load the application MSP before invoking RSS: the ROM service
     * still needs the secure bootloader stack while it closes the internal
     * Flash secure area.  RSS consumes the vector table and performs the
     * final transfer.  A direct branch would leave Kdev readable by QSPI code.
     */
    HBoxSecureAccess_ExitToApplication(app_base_address);
#else
    // 设置主堆栈指针
    __set_MSP(app_stack);
    uint32_t current_msp = __get_MSP();
    if (current_msp != app_stack) {
        BOOT_ERR("Stack pointer setup failed: Expected=0x%08lX, Actual=0x%08lX", 
                 (unsigned long)app_stack, (unsigned long)current_msp);
        Logger_Log(LOG_LEVEL_ERROR, "STACK", "Stack pointer setup failed: Expected=0x%08lX, Actual=0x%08lX", 
                         (unsigned long)app_stack, (unsigned long)current_msp);
        return;
    }

    // 内存屏障确保所有操作完成
    __DSB();
    __ISB();

    // 确保跳转地址包含Thumb位
    jump_address |= 0x1;

    // 创建函数指针
    typedef void (*pFunction)(void);
    pFunction app_reset_handler = (pFunction)jump_address;
    
    // 最后一次内存屏障
    __DSB();
    __ISB();
    
    // 跳转到应用程序
    app_reset_handler();
#endif

    // 不应该到达这里
    BOOT_STAGE_ERROR("B16", "application transfer returned unexpectedly");
    BOOT_ERR("Jump failed! Program should not return here");
    Logger_Log(LOG_LEVEL_FATAL, "JUMP", "Jump to application failed - should not return here");
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
  Logger_Log(LOG_LEVEL_ERROR, "ASSERT", "Assert failed: %s:%lu", file, (unsigned long)line);
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
