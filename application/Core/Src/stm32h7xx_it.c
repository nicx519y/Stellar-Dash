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

    // 解析 CFSR
    if (cfsr & (1 << 0))  APP_DBG("MMFSR: Instruction access violation");
    if (cfsr & (1 << 1))  APP_DBG("MMFSR: Data access violation");
    if (cfsr & (1 << 16)) APP_DBG("BFSR: Instruction bus error");
    if (cfsr & (1 << 17)) APP_DBG("BFSR: Precise data bus error");
    if (cfsr & (1 << 24)) APP_DBG("UFSR: Undefined instruction");
    if (cfsr & (1 << 25)) APP_DBG("UFSR: Invalid state");
    if (cfsr & (1 << 26)) APP_DBG("UFSR: Invalid PC");
    if (cfsr & (1 << 27)) APP_DBG("UFSR: No coprocessor");

    while(1);
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  printf("MemManage_Handler!\n");
  while(1);
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  printf("BusFault_Handler!\n");
  while(1);
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
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


