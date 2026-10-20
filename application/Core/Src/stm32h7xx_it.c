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
#include "board_cfg.h"
#include "system_logger.h"
extern void XoraLog_CaptureFault(uint32_t *, uint32_t, uint32_t);
#include "st7789.h"
#include "spi-st7789.h"
#include "rotary-encoder.h"
#include "system_sleep_manager.hpp"
#include "system_stop.hpp"
#include "rf_bridge_port_internal.h"
#include "ch585_handshake.h"
#include <stdio.h>
/* USER CODE END Includes */

extern void PowerManager_NotifyChargerIrqFromISR(void);
extern void PowerManager_NotifyGaugeAlertFromISR(void);

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
extern DMA_HandleTypeDef hdma_adc1;
extern DMA_HandleTypeDef hdma_adc2;
extern DMA_HandleTypeDef hdma_adc3;
extern DMA_HandleTypeDef hdma_tim4_ch1;
extern DMA_HandleTypeDef hdma_tim4_ch2;
extern TIM_HandleTypeDef htim2;
/* USER CODE BEGIN EV */
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
__attribute__((naked)) void NMI_Handler(void)
{
    __asm volatile("tst lr, #4\n" "ite eq\n" "mrseq r0, msp\n" "mrsne r0, psp\n"
                   "ldr r3, =XoraLog_FaultStack + 512\n" "mov sp, r3\n" "mov r1, lr\n" "movs r2, #2\n" "b XoraLog_CaptureFault\n");
}

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst lr, #4\n" "ite eq\n" "mrseq r0, msp\n" "mrsne r0, psp\n"
                   "ldr r3, =XoraLog_FaultStack + 512\n" "mov sp, r3\n" "mov r1, lr\n" "movs r2, #3\n" "b XoraLog_CaptureFault\n");
}

/**
  * @brief This function handles Memory management fault.
  */
__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile("tst lr, #4\n" "ite eq\n" "mrseq r0, msp\n" "mrsne r0, psp\n"
                   "ldr r3, =XoraLog_FaultStack + 512\n" "mov sp, r3\n" "mov r1, lr\n" "movs r2, #4\n" "b XoraLog_CaptureFault\n");
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
__attribute__((naked)) void BusFault_Handler(void)
{
    __asm volatile("tst lr, #4\n" "ite eq\n" "mrseq r0, msp\n" "mrsne r0, psp\n"
                   "ldr r3, =XoraLog_FaultStack + 512\n" "mov sp, r3\n" "mov r1, lr\n" "movs r2, #5\n" "b XoraLog_CaptureFault\n");
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
__attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile("tst lr, #4\n" "ite eq\n" "mrseq r0, msp\n" "mrsne r0, psp\n"
                   "ldr r3, =XoraLog_FaultStack + 512\n" "mov sp, r3\n" "mov r1, lr\n" "movs r2, #6\n" "b XoraLog_CaptureFault\n");
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
  RotEnc_Tick1msFromISR();
  SystemSleep_Tick1msFromISR();
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

void DMA1_Stream3_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_tim4_ch2);
}

void DMA1_Stream4_IRQHandler(void)
{
  SPIST7789_DMA_IRQHandler();
}

void DMA2_Stream1_IRQHandler(void)
{
  SPIST7789_DMA_IRQHandler();
}

void DMA2_Stream2_IRQHandler(void)
{
  SPIST7789_DMA_IRQHandler();
}

void DMA2_Stream4_IRQHandler(void)
{
  SPIST7789_DMA_IRQHandler();
}

void DMA2_Stream5_IRQHandler(void)
{
  RFBridgePort_DMA_IRQHandler();
}

void SPI4_IRQHandler(void)
{
  RFBridgePort_SPI_IRQHandler();
}

void SPI1_IRQHandler(void)
{
  SPIST7789_SPI_IRQHandler();
}

/**
  * @brief This function handles EXTI line[9:5] interrupts.
  */
void EXTI9_5_IRQHandler(void)
{
  if (SystemStop_HandleKeyIRQ()) return;
  if (__HAL_GPIO_EXTI_GET_IT(CHARGE_INT_PIN) != RESET) {
    __HAL_GPIO_EXTI_CLEAR_IT(CHARGE_INT_PIN);
    PowerManager_NotifyChargerIrqFromISR();
  }
}

/**
  * @brief This function handles EXTI line[15:10] interrupts.
  */
void EXTI15_10_IRQHandler(void)
{
  if (__HAL_GPIO_EXTI_GET_IT(RF_BRIDGE_IRQ_PIN) != RESET) {
    Ch585Handshake_IRQHandler();
  }
  if (__HAL_GPIO_EXTI_GET_IT(MAX17048_ALERT_PIN) != RESET) {
    __HAL_GPIO_EXTI_CLEAR_IT(MAX17048_ALERT_PIN);
    PowerManager_NotifyGaugeAlertFromISR();
  }
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


