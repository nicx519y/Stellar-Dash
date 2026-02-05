/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    stm32h7xx_it.c
 * @brief   Interrupt Service Routines.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
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
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "device/usbd.h"
#include "board_cfg.h"
#include "usbh.h"
#include "system_logger.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

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
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
extern DMA_HandleTypeDef hdma_adc1;
extern DMA_HandleTypeDef hdma_adc2;
extern DMA_HandleTypeDef hdma_adc3;
extern DMA_HandleTypeDef hdma_tim4_ch1;
extern TIM_HandleTypeDef htim2;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */
  
  // 记录NMI中断到日志
  LOG_FATAL("NMI", "Non-Maskable Interrupt occurred");
  
  // 强制刷新日志
  Logger_Flush();

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
  while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
    // 获取 MSP 和 PSP
    uint32_t msp = __get_MSP();
    uint32_t psp = __get_PSP();
    
    // 获取故障状态寄存器
    volatile uint32_t cfsr = SCB->CFSR;    // Configurable Fault Status Register
    volatile uint32_t hfsr = SCB->HFSR;    // HardFault Status Register
    volatile uint32_t dfsr = SCB->DFSR;    // Debug Fault Status Register
    volatile uint32_t mmfar = SCB->MMFAR;  // MemManage Fault Address Register
    volatile uint32_t bfar = SCB->BFAR;    // BusFault Address Register
    
    // 获取 FPU 状态
    uint32_t fpscr = __get_FPSCR();
    uint32_t cpacr = SCB->CPACR;  // Coprocessor Access Control Register

    // 获取调用栈信息
    uint32_t *stack;
    if (__get_CONTROL() & 0x02) {    // 使用 CMSIS 函数替代直接访问寄存器
        stack = (uint32_t *)psp;
    } else {
        stack = (uint32_t *)msp;
    }

    // 记录致命错误到日志系统
    LOG_FATAL("HARDFAULT", "HardFault exception occurred");
    LOG_FATAL("HARDFAULT", "MSP=0x%08lX PSP=0x%08lX", msp, psp);
    LOG_FATAL("HARDFAULT", "CFSR=0x%08lX HFSR=0x%08lX", cfsr, hfsr);
    LOG_FATAL("HARDFAULT", "MMFAR=0x%08lX BFAR=0x%08lX", mmfar, bfar);
    LOG_FATAL("HARDFAULT", "PC=0x%08lX LR=0x%08lX", stack[6], stack[5]);

    // 打印调试信息
    APP_DBG("\r\n[HardFault]");
    APP_DBG("MSP: 0x%08lX", msp);
    APP_DBG("PSP: 0x%08lX", psp);
    APP_DBG("CFSR: 0x%08lX", cfsr);
    APP_DBG("HFSR: 0x%08lX", hfsr);
    APP_DBG("DFSR: 0x%08lX", dfsr);
    APP_DBG("MMFAR: 0x%08lX", mmfar);
    APP_DBG("BFAR: 0x%08lX", bfar);
    APP_DBG("FPSCR: 0x%08lX", fpscr);
    APP_DBG("CPACR: 0x%08lX", cpacr);
    APP_DBG("CONTROL: 0x%08lX", __get_CONTROL());

    // 打印调用栈
    APP_DBG("\r\nCall Stack:");
    APP_DBG("R0:  0x%08lX", stack[0]);
    APP_DBG("R1:  0x%08lX", stack[1]);
    APP_DBG("R2:  0x%08lX", stack[2]);
    APP_DBG("R3:  0x%08lX", stack[3]);
    APP_DBG("R12: 0x%08lX", stack[4]);
    APP_DBG("LR:  0x%08lX", stack[5]);
    APP_DBG("PC:  0x%08lX", stack[6]);
    APP_DBG("PSR: 0x%08lX", stack[7]);

    // 解析 CFSR 并记录到日志
    if (cfsr & (1 << 0)) {
        APP_DBG("MMFSR: Instruction access violation");
        LOG_FATAL("HARDFAULT", "Memory management fault: Instruction access violation");
    }
    if (cfsr & (1 << 1)) {
        APP_DBG("MMFSR: Data access violation");
        LOG_FATAL("HARDFAULT", "Memory management fault: Data access violation");
    }
    if (cfsr & (1 << 16)) {
        APP_DBG("BFSR: Instruction bus error");
        LOG_FATAL("HARDFAULT", "Bus fault: Instruction bus error");
    }
    if (cfsr & (1 << 17)) {
        APP_DBG("BFSR: Precise data bus error");
        LOG_FATAL("HARDFAULT", "Bus fault: Precise data bus error");
    }
    if (cfsr & (1 << 24)) {
        APP_DBG("UFSR: Undefined instruction");
        LOG_FATAL("HARDFAULT", "Usage fault: Undefined instruction");
    }
    if (cfsr & (1 << 25)) {
        APP_DBG("UFSR: Invalid state");
        LOG_FATAL("HARDFAULT", "Usage fault: Invalid state");
    }
    if (cfsr & (1 << 26)) {
        APP_DBG("UFSR: Invalid PC");
        LOG_FATAL("HARDFAULT", "Usage fault: Invalid PC");
    }
    if (cfsr & (1 << 27)) {
        APP_DBG("UFSR: No coprocessor");
        LOG_FATAL("HARDFAULT", "Usage fault: No coprocessor");
    }

    // 强制刷新日志到Flash
    Logger_Flush();

    while(1);
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  // 记录内存管理错误到日志
  LOG_FATAL("MEMMANAGE", "Memory Management Fault occurred");
  LOG_FATAL("MEMMANAGE", "MMFAR=0x%08lX CFSR=0x%08lX", SCB->MMFAR, SCB->CFSR);
  
  // 解析具体错误原因
  uint32_t cfsr = SCB->CFSR;
  if (cfsr & (1 << 0)) LOG_FATAL("MEMMANAGE", "Instruction access violation");
  if (cfsr & (1 << 1)) LOG_FATAL("MEMMANAGE", "Data access violation");
  if (cfsr & (1 << 3)) LOG_FATAL("MEMMANAGE", "MemManage fault on unstacking for exception return");
  if (cfsr & (1 << 4)) LOG_FATAL("MEMMANAGE", "MemManage fault on stacking for exception entry");
  if (cfsr & (1 << 5)) LOG_FATAL("MEMMANAGE", "MemManage fault during FP lazy state preservation");
  if (cfsr & (1 << 7)) LOG_FATAL("MEMMANAGE", "MMFAR holds valid fault address");

  // 强制刷新日志
  Logger_Flush();
  
  printf("MemManage_Handler!\n");
  while(1);
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  // 记录总线错误到日志
  LOG_FATAL("BUSFAULT", "Bus Fault occurred");
  LOG_FATAL("BUSFAULT", "BFAR=0x%08lX CFSR=0x%08lX", SCB->BFAR, SCB->CFSR);
  
  // 解析具体错误原因
  uint32_t cfsr = SCB->CFSR;
  if (cfsr & (1 << 8)) LOG_FATAL("BUSFAULT", "Instruction bus error");
  if (cfsr & (1 << 9)) LOG_FATAL("BUSFAULT", "Precise data bus error");
  if (cfsr & (1 << 10)) LOG_FATAL("BUSFAULT", "Imprecise data bus error");
  if (cfsr & (1 << 11)) LOG_FATAL("BUSFAULT", "BusFault on unstacking for exception return");
  if (cfsr & (1 << 12)) LOG_FATAL("BUSFAULT", "BusFault on stacking for exception entry");
  if (cfsr & (1 << 13)) LOG_FATAL("BUSFAULT", "BusFault during FP lazy state preservation");
  if (cfsr & (1 << 15)) LOG_FATAL("BUSFAULT", "BFAR holds valid fault address");

  // 强制刷新日志
  Logger_Flush();
  
  printf("BusFault_Handler!\n");
  while(1);
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  // 记录使用错误到日志
  LOG_FATAL("USAGEFAULT", "Usage Fault occurred");
  LOG_FATAL("USAGEFAULT", "CFSR=0x%08lX", SCB->CFSR);
  
  // 解析具体错误原因
  uint32_t cfsr = SCB->CFSR;
  if (cfsr & (1 << 16)) LOG_FATAL("USAGEFAULT", "Undefined instruction");
  if (cfsr & (1 << 17)) LOG_FATAL("USAGEFAULT", "Invalid state");
  if (cfsr & (1 << 18)) LOG_FATAL("USAGEFAULT", "Invalid PC load");
  if (cfsr & (1 << 19)) LOG_FATAL("USAGEFAULT", "No coprocessor");
  if (cfsr & (1 << 20)) LOG_FATAL("USAGEFAULT", "Unaligned access");
  if (cfsr & (1 << 24)) LOG_FATAL("USAGEFAULT", "Divide by zero");

  // 强制刷新日志
  Logger_Flush();
  
  printf("UsageFault_Handler!\n");
  while(1);
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  #if (INCLUDE_xTaskGetSchedulerState == 1 )
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
  #endif /* INCLUDE_xTaskGetSchedulerState */
    HAL_IncTick();
  #if (INCLUDE_xTaskGetSchedulerState == 1 )
    }
  #endif /* INCLUDE_xTaskGetSchedulerState */
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h7xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles DMA1 stream0 global interrupt.
  */
void DMA1_Stream0_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream0_IRQn 0 */

  /* USER CODE END DMA1_Stream0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc1);
  /* USER CODE BEGIN DMA1_Stream0_IRQn 1 */

  /* USER CODE END DMA1_Stream0_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream1 global interrupt.
  */
void DMA1_Stream1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream1_IRQn 0 */

  /* USER CODE END DMA1_Stream1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc2);
  /* USER CODE BEGIN DMA1_Stream1_IRQn 1 */

  /* USER CODE END DMA1_Stream1_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream2 global interrupt.
  */
void DMA1_Stream2_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream2_IRQn 0 */

  /* USER CODE END DMA1_Stream2_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_tim4_ch1);
  /* USER CODE BEGIN DMA1_Stream2_IRQn 1 */

  /* USER CODE END DMA1_Stream2_IRQn 1 */
}

/**
  * @brief This function handles TIM2 global interrupt.
  */
void TIM2_IRQHandler(void)
{
  /* USER CODE BEGIN TIM2_IRQn 0 */

  /* USER CODE END TIM2_IRQn 0 */
  HAL_TIM_IRQHandler(&htim2);
  /* USER CODE BEGIN TIM2_IRQn 1 */

  /* USER CODE END TIM2_IRQn 1 */
}

extern HCD_HandleTypeDef hhcd_USB_OTG_HS;
/**
  * @brief This function handles USB On The Go HS global interrupt.
  */
void OTG_HS_IRQHandler(void)
{
  tuh_int_handler(1);
}

/**
  * @brief This function handles USB On The Go FS global interrupt.
  */
void OTG_FS_IRQHandler(void)
{
  tud_int_handler(0);
}

/* USER CODE BEGIN 1 */
/**
  * @brief This function handles BDMA channel0 global interrupt.
  */
void BDMA_Channel0_IRQHandler(void)
{
  /* USER CODE BEGIN BDMA_Channel0_IRQn 0 */

  /* USER CODE END BDMA_Channel0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc3);
  /* USER CODE BEGIN BDMA_Channel0_IRQn 1 */

  /* USER CODE END BDMA_Channel0_IRQn 1 */
}
/* USER CODE END 1 */


