/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usb_otg.c
  * @brief   This file provides code for the configuration
  *          of the USB_OTG instances.
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
#include "usb_otg.h"
#include "board_cfg.h"
#include <stdbool.h>

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

HCD_HandleTypeDef hhcd_USB_OTG_HS;

/* USB_OTG_HS init function */

void MX_USB_OTG_HS_HCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_HS_Init 0 */

  /* USER CODE END USB_OTG_HS_Init 0 */

  /* USER CODE BEGIN USB_OTG_HS_Init 1 */

  /* USER CODE END USB_OTG_HS_Init 1 */
  hhcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hhcd_USB_OTG_HS.Init.Host_channels = 16;
  hhcd_USB_OTG_HS.Init.speed = HCD_SPEED_FULL;
  hhcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hhcd_USB_OTG_HS.Init.phy_itface = USB_OTG_EMBEDDED_PHY;
  hhcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
  hhcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hhcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
  if (HAL_HCD_Init(&hhcd_USB_OTG_HS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_HS_Init 2 */

  /* USER CODE END USB_OTG_HS_Init 2 */

}

void HAL_HCD_MspInit(HCD_HandleTypeDef* hcdHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(hcdHandle->Instance==USB_OTG_HS)
  {
  /* USER CODE BEGIN USB_OTG_HS_MspInit 0 */

  /* USER CODE END USB_OTG_HS_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

  /** Enable USB Voltage detector
  */
    HAL_PWREx_EnableUSBVoltageDetector();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**USB_OTG_HS GPIO Configuration
    PB15     ------> USB_OTG_HS_DP
    PB14     ------> USB_OTG_HS_DM
    */
    GPIO_InitStruct.Pin = GPIO_PIN_15|GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_OTG2_FS;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* USB_OTG_HS clock enable */
    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();

    /* USB_OTG_HS interrupt Init */
    HAL_NVIC_SetPriority(OTG_HS_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
  /* USER CODE BEGIN USB_OTG_HS_MspInit 1 */
    APP_DBG("USB_OTG_HS_MspInit");
  /* USER CODE END USB_OTG_HS_MspInit 1 */
  }
}

void HAL_HCD_MspDeInit(HCD_HandleTypeDef* hcdHandle)
{

  if(hcdHandle->Instance==USB_OTG_HS)
  {
  /* USER CODE BEGIN USB_OTG_HS_MspDeInit 0 */

  /* USER CODE END USB_OTG_HS_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();

    /**USB_OTG_HS GPIO Configuration
    PB15     ------> USB_OTG_HS_DP
    PB14     ------> USB_OTG_HS_DM
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_15|GPIO_PIN_14);

    /* USB_OTG_HS interrupt Deinit */
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
  /* USER CODE BEGIN USB_OTG_HS_MspDeInit 1 */

  /* USER CODE END USB_OTG_HS_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

// USB连接回调
void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hhcd)
{
  APP_DBG("USB device connected");
  // 可以在这里添加设备枚举代码
}

// USB断开回调
void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hhcd)
{
  APP_DBG("USB device disconnected");
}

// USB端口启用回调
void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *hhcd)
{
  APP_DBG("USB port enabled");
}

// USB端口禁用回调
void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hhcd)
{
  APP_DBG("USB port disabled");
}

void USB_DiagnoseInterruptConfig(void)
{
  APP_DBG("USB Interrupt Configuration Diagnosis:");
  
  // 检查NVIC中断状态
  bool nvic_enabled = (NVIC->ISER[OTG_HS_IRQn/32] & (1 << (OTG_HS_IRQn % 32))) != 0;
  APP_DBG("1. NVIC Interrupt Status: %s", nvic_enabled ? "Enabled" : "Disabled ⚠️");
  
  // 检查全局中断使能
  bool gint_enabled = (USB_OTG_HS->GAHBCFG & USB_OTG_GAHBCFG_GINT) != 0;
  APP_DBG("2. Global Interrupt Enable: %s", gint_enabled ? "Enabled" : "Disabled ⚠️");
  
  // 检查端口中断掩码
  bool prtint_enabled = (USB_OTG_HS->GINTMSK & USB_OTG_GINTMSK_PRTIM) != 0;
  APP_DBG("3. Port Interrupt Mask: %s", prtint_enabled ? "Enabled" : "Disabled ⚠️");
  
  // 检查断开连接中断掩码
  bool discint_enabled = (USB_OTG_HS->GINTMSK & USB_OTG_GINTMSK_DISCINT) != 0;
  APP_DBG("4. Disconnect Interrupt Mask: %s", discint_enabled ? "Enabled" : "Disabled ⚠️");
  
  // 检查控制器模式
  bool host_mode = (USB_OTG_HS->GUSBCFG & USB_OTG_GUSBCFG_FHMOD) != 0;
  bool dev_mode = (USB_OTG_HS->GUSBCFG & USB_OTG_GUSBCFG_FDMOD) != 0;
  APP_DBG("5. Controller Mode: %s", 
         host_mode ? "Host Mode" : (dev_mode ? "Device Mode ⚠️" : "Undefined Mode ⚠️"));
  
  // 检查端口电源
  uint32_t hprt = *(__IO uint32_t *)((uint32_t)USB_OTG_HS + 0x440); // HPRT的偏移量是0x440
  bool ppwr_enabled = (hprt & 0x00001000) != 0; // PPWR位是0x00001000
  APP_DBG("6. Port Power Status: %s", ppwr_enabled ? "Enabled" : "Disabled ⚠️");
  
  // 添加HPRT寄存器状态
  APP_DBG("7. HPRT Register Value: 0x%08lX", hprt);
  APP_DBG("   - Connected: %s", (hprt & 0x00000001) ? "Yes" : "No"); // PCSTS位
  APP_DBG("   - Port Reset: %s", (hprt & 0x00000010) ? "Active" : "Inactive"); // PRST位
  APP_DBG("   - Port Enable: %s", (hprt & 0x00000004) ? "Enabled" : "Disabled"); // PENA位
  
  // 检查芯片ID以确认是否为STM32H750
  uint32_t dbgmcu_idcode = *(uint32_t *)0xE0042000;
  uint16_t dev_id = dbgmcu_idcode & 0xFFF;
  APP_DBG("8. Chip ID: 0x%03X (0x450=STM32H7)", dev_id);
  
  // 总结
  APP_DBG("\r\nDiagnosis Result: ");
  if (nvic_enabled && gint_enabled && prtint_enabled && host_mode && ppwr_enabled) {
    APP_DBG("All configurations correct ✓");
  } else {
    APP_DBG("Configuration issues found ⚠️");
  }
  APP_DBG("--------------------------------");
}

/**
 * 修复USB中断和端口电源配置
 * 根据诊断结果修复常见的USB主机模式配置问题
 */
void USB_FixInterrupts(void)
{
  APP_DBG("Fixing USB Interrupt and Port configuration...");
  
  // 1. 确保NVIC中断已启用
  if (!(NVIC->ISER[OTG_HS_IRQn/32] & (1 << (OTG_HS_IRQn % 32)))) {
    HAL_NVIC_SetPriority(OTG_HS_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
    APP_DBG("- Enabled NVIC interrupt for OTG_HS");
  }
  
  // 2. 启用全局中断
  if (!(USB_OTG_HS->GAHBCFG & USB_OTG_GAHBCFG_GINT)) {
    USB_OTG_HS->GAHBCFG |= USB_OTG_GAHBCFG_GINT;
    APP_DBG("- Enabled global USB interrupts");
  }
  
  // 3. 设置中断掩码
  uint32_t old_mask = USB_OTG_HS->GINTMSK;
  APP_DBG("- Previous interrupt mask: 0x%08lX", old_mask);
  
  // 启用关键中断 (端口中断和断开连接中断)
  USB_OTG_HS->GINTMSK |= USB_OTG_GINTMSK_PRTIM;  // 端口中断
  USB_OTG_HS->GINTMSK |= USB_OTG_GINTMSK_DISCINT; // 断开连接中断
  APP_DBG("- Updated interrupt mask: 0x%08lX", USB_OTG_HS->GINTMSK);
  
  // 4. 确保控制器处于主机模式
  if (!(USB_OTG_HS->GUSBCFG & USB_OTG_GUSBCFG_FHMOD)) {
    // 强制主机模式
    USB_OTG_HS->GUSBCFG |= USB_OTG_GUSBCFG_FHMOD;  // 设置强制主机模式
    USB_OTG_HS->GUSBCFG &= ~USB_OTG_GUSBCFG_FDMOD; // 清除设备模式位
    HAL_Delay(50);  // 给控制器时间切换模式
    APP_DBG("- Forced host mode");
  }
  
  // 5. 清除所有挂起中断
  uint32_t pending = USB_OTG_HS->GINTSTS;
  APP_DBG("- Pending interrupts: 0x%08lX", pending);
  USB_OTG_HS->GINTSTS = 0xFFFFFFFF; // 清除所有中断
  
  // 6. 开启端口电源
  uint32_t hprt = *(__IO uint32_t *)((uint32_t)USB_OTG_HS + 0x440);
  APP_DBG("- Current HPRT: 0x%08lX", hprt);
  
  // 必须只设置PPWR位，同时不清除其他状态位，避免意外清除
  uint32_t new_hprt = hprt & ~(0x00000004U | 0x00000008U | 0x00000010U); // 不改变PENA, PCDET, PENCHNG, POCCHNG位
  new_hprt |= 0x00001000; // 设置PPWR位（端口电源）
  
  *(__IO uint32_t *)((uint32_t)USB_OTG_HS + 0x440) = new_hprt;
  APP_DBG("- Updated HPRT: 0x%08lX", *(__IO uint32_t *)((uint32_t)USB_OTG_HS + 0x440));
  
  APP_DBG("USB Interrupt and Port fix completed");
  
  // 诊断修复后的状态
  USB_DiagnoseInterruptConfig();
}

/* USER CODE END 1 */
