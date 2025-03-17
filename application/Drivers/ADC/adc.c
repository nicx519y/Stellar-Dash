/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    adc.c
 * @brief   This file provides code for the configuration
 *          of the ADC instances.
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

#include "adc.h"
#include "board_cfg.h"

/* USER CODE BEGIN 0 */

/*
|--------------------|------------------|--------|-------|-------|
|OVERSAMPLE_RATIO    |ADC_SAMPLE_TIME   |NOISE   |CROSS  |FREQ   |
|--------------------|------------------|--------|-------|-------|
|                  64|            32.5  |      46|    24 |  1142 |
|                  32|            32.5  |      60|    44 |  2283 |
|                  32|            64.5  |      48|    26 |  1282 |
|                  16|           387.5  |      28|    15 |   473 |
|                   4|           387.5  |      58|    40 |  1890 |
|                   2|           387.5  |      78|    43 |  3787 |
|                  64|            16.5  |      54|    39 |  1872 |
|                  64|             8.5  |      68|    48 |  2754 |
|                 128|             8.5  |      50|    28 |  1377 |
|                  64|            16.5  |      56|    38 |  1872 |
|                 128|             2.5  |      58|    42 |  2127 |
|                 128|             8.5  |      52|    28 |  1377 |
|--------------------|------------------|--------|-------|-------|
*/

#define ADC_OVERSAMPLE_RATIO               256         //ADC过采样倍数
#define ADC_OVERSAMPLE_RIGHT_BIT_SHIFT     ADC_RIGHTBITSHIFT_8 //ADC过采样右移位数
#define ADC_SAMPLE_TIME                    ADC_SAMPLETIME_64CYCLES_5       //ADC采样时间


void ADC_Clock_Init(void)
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
    PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL3;  // ADC时钟频率 HSE: 24MHz，ADC时钟频率 = HSE / PLL3.PLL3M * PLL3.PLL3N / PLL3.PLL3R = 24MHz / 2 * 15 / 5 = 36MHz
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
}
/* USER CODE END 0 */

ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
ADC_HandleTypeDef hadc3;
DMA_HandleTypeDef hdma_adc1;
DMA_HandleTypeDef hdma_adc2;
DMA_HandleTypeDef hdma_adc3;

/* ADC1 init function */
void MX_ADC1_Init(void)
{
    /* USER CODE BEGIN ADC1_Init 0 */
    /* USER CODE END ADC1_Init 0 */

    ADC_MultiModeTypeDef multimode = {0};
    ADC_ChannelConfTypeDef sConfig = {0};

    /* USER CODE BEGIN ADC1_Init 1 */
    /* USER CODE END ADC1_Init 1 */

    /** Common config */
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
    hadc1.Init.Resolution = ADC_RESOLUTION_16B;
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.NbrOfConversion = NUM_ADC1_BUTTONS;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    hadc1.Init.OversamplingMode = ENABLE;
    hadc1.Init.Oversampling.Ratio = ADC_OVERSAMPLE_RATIO;
    hadc1.Init.Oversampling.RightBitShift = ADC_OVERSAMPLE_RIGHT_BIT_SHIFT;
    hadc1.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
    hadc1.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;

    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    /** Configure the ADC multi-mode
     */
    multimode.Mode = ADC_MODE_INDEPENDENT;
    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
    {
        Error_Handler();
    }

    /** Configure Regular Channel
     */
    sConfig.Channel = ADC_CHANNEL_2;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLE_TIME;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /** Configure Regular Channel
     */
    sConfig.Channel = ADC_CHANNEL_3;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = ADC_REGULAR_RANK_3;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_5;
    sConfig.Rank = ADC_REGULAR_RANK_4;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_6;
    sConfig.Rank = ADC_REGULAR_RANK_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_7;
    sConfig.Rank = ADC_REGULAR_RANK_6;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }


    /* USER CODE BEGIN ADC1_Init 2 */

    if(!__HAL_RCC_ADC12_IS_CLK_ENABLED()) {
        APP_ERR("ADC12 Clock not enabled!");
    }

    // 检查 DMA 时钟
    if(!__HAL_RCC_DMA1_IS_CLK_ENABLED()) {
        APP_ERR("DMA1 Clock not enabled!");
    }

    /* USER CODE END ADC1_Init 2 */
}
/* ADC2 init function */
void MX_ADC2_Init(void)
{
    /* USER CODE BEGIN ADC2_Init 0 */
    /* USER CODE END ADC2_Init 0 */

    ADC_ChannelConfTypeDef sConfig = {0};

    /* USER CODE BEGIN ADC2_Init 1 */
    /* USER CODE END ADC2_Init 1 */

    /** Common config */
    hadc2.Instance = ADC2;
    hadc2.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
    hadc2.Init.Resolution = ADC_RESOLUTION_16B;
    hadc2.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc2.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    hadc2.Init.LowPowerAutoWait = DISABLE;
    hadc2.Init.ContinuousConvMode = ENABLE;
    hadc2.Init.NbrOfConversion = NUM_ADC2_BUTTONS;
    hadc2.Init.DiscontinuousConvMode = DISABLE;
    hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc2.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
    hadc2.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc2.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    hadc2.Init.OversamplingMode = ENABLE;
    hadc2.Init.Oversampling.Ratio = ADC_OVERSAMPLE_RATIO;
    hadc2.Init.Oversampling.RightBitShift = ADC_OVERSAMPLE_RIGHT_BIT_SHIFT;
    hadc2.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
    hadc2.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;

    if (HAL_ADC_Init(&hadc2) != HAL_OK)
    {
        Error_Handler();
    }

    /** Configure Regular Channel
     */
    sConfig.Channel = ADC_CHANNEL_2;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLE_TIME;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /** Configure Regular Channel
     */
    sConfig.Channel = ADC_CHANNEL_6;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_8;
    sConfig.Rank = ADC_REGULAR_RANK_3;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_9;
    sConfig.Rank = ADC_REGULAR_RANK_4;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_10;
    sConfig.Rank = ADC_REGULAR_RANK_5;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }
    
    sConfig.Channel = ADC_CHANNEL_11;
    sConfig.Rank = ADC_REGULAR_RANK_6;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN ADC2_Init 2 */

    if(!__HAL_RCC_ADC12_IS_CLK_ENABLED()) {
        APP_ERR("ADC12 Clock not enabled for ADC2!");
    }
    
    // 检查 ADC2 状态
    APP_DBG("ADC2 State after init: 0x%x", HAL_ADC_GetState(&hadc2));

    // 检查 DMA 时钟
    if(!__HAL_RCC_DMA1_IS_CLK_ENABLED()) {
        APP_ERR("DMA1 Clock not enabled for ADC2!");
    }

    /* USER CODE END ADC2_Init 2 */
}

void MX_ADC3_Init(void)
{   
    /* USER CODE BEGIN ADC3_Init 0 */
    /* USER CODE END ADC3_Init 0 */

    ADC_ChannelConfTypeDef sConfig = {0};

    hadc3.Instance = ADC3;
    hadc3.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
    hadc3.Init.Resolution = ADC_RESOLUTION_16B;
    hadc3.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc3.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    hadc3.Init.LowPowerAutoWait = DISABLE;
    hadc3.Init.ContinuousConvMode = ENABLE;
    hadc3.Init.NbrOfConversion = NUM_ADC3_BUTTONS;
    hadc3.Init.DiscontinuousConvMode = DISABLE;
    hadc3.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc3.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc3.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
    hadc3.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc3.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    hadc3.Init.OversamplingMode = ENABLE;
    hadc3.Init.Oversampling.Ratio = ADC_OVERSAMPLE_RATIO;
    hadc3.Init.Oversampling.RightBitShift = ADC_OVERSAMPLE_RIGHT_BIT_SHIFT;
    hadc3.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
    hadc3.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;

    if (HAL_ADC_Init(&hadc3) != HAL_OK)
    {
        Error_Handler();
    }

    /** Configure Regular Channel
     */
    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLE_TIME;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    if (HAL_ADC_ConfigChannel(&hadc3, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /** Configure Regular Channel
     */
    sConfig.Channel = ADC_CHANNEL_5;
    sConfig.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&hadc3, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_9;
    sConfig.Rank = ADC_REGULAR_RANK_3;
    if (HAL_ADC_ConfigChannel(&hadc3, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_12;
    sConfig.Rank = ADC_REGULAR_RANK_4;
    if (HAL_ADC_ConfigChannel(&hadc3, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfig.Channel = ADC_CHANNEL_13;
    sConfig.Rank = ADC_REGULAR_RANK_5;
    if (HAL_ADC_ConfigChannel(&hadc3, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    // 检查 ADC3 时钟
    if(!__HAL_RCC_ADC3_IS_CLK_ENABLED()) {
        APP_ERR("ADC3 Clock not enabled!");
    }
    
    if(!__HAL_RCC_BDMA_IS_CLK_ENABLED()) {
        APP_ERR("BDMA Clock not enabled!");
    }
    
    /* USER CODE BEGIN ADC3_Init 1 */
    // ADC3 BDMA 中断配置
    HAL_NVIC_SetPriority(BDMA_Channel0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(BDMA_Channel0_IRQn);
    /* USER CODE END ADC3_Init 1 */
}

static uint32_t HAL_RCC_ADC12_CLK_ENABLED = 0;

void HAL_ADC_MspInit(ADC_HandleTypeDef *adcHandle)
{

    ADC_Clock_Init();


    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (adcHandle->Instance == ADC1)
    {
        /* USER CODE BEGIN ADC1_MspInit 0 */

        /* USER CODE END ADC1_MspInit 0 */
        /* ADC1 clock enable */
        HAL_RCC_ADC12_CLK_ENABLED++;
        if (HAL_RCC_ADC12_CLK_ENABLED == 1)
        {
            __HAL_RCC_ADC12_CLK_ENABLE();
        }

        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOF_CLK_ENABLE();
        /**ADC1 GPIO Configuration
        PF11     ------> ADC1_INP2
        PA6     ------> ADC1_INP3
        PC4     ------> ADC1_INP4
        PB1     ------> ADC1_INP5
        PF12     ------> ADC1_INP6
        PA7     ------> ADC1_INP7
        */
        GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_1;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_4;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

        /* ADC1 DMA Init */
        /* ADC1 Init */
        hdma_adc1.Instance = DMA1_Stream0;
        hdma_adc1.Init.Request = DMA_REQUEST_ADC1; 
        hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
        hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
        hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
        hdma_adc1.Init.Mode = DMA_CIRCULAR;
        hdma_adc1.Init.Priority = DMA_PRIORITY_VERY_HIGH;
        hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
        {
            Error_Handler();
        }

        __HAL_LINKDMA(adcHandle, DMA_Handle, hdma_adc1);

        /* USER CODE BEGIN ADC1_MspInit 1 */
        // ADC1 DMA 中断配置
        HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
        /* USER CODE END ADC1_MspInit 1 */
    }
    else if (adcHandle->Instance == ADC2)
    {
        /* USER CODE BEGIN ADC2_MspInit 0 */

        /* USER CODE END ADC2_MspInit 0 */
        /* ADC2 clock enable */
        HAL_RCC_ADC12_CLK_ENABLED++;
        if (HAL_RCC_ADC12_CLK_ENABLED == 1)
        {
            __HAL_RCC_ADC12_CLK_ENABLE();
        }

        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOF_CLK_ENABLE();
        /**ADC2 GPIO Configuration
        PF13     ------> ADC2_INP2
        PF14     ------> ADC2_INP6
        PC5     ------> ADC2_INP8
        PB0     ------> ADC2_INP9
        PC0     ------> ADC2_INP10
        PC1     ------> ADC2_INP11
        */
        GPIO_InitStruct.Pin = GPIO_PIN_0;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_5;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_13 | GPIO_PIN_14;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

        /* ADC2 DMA Init */
        /* ADC2 Init */
        APP_DBG("Configuring ADC2 DMA...");
        
        hdma_adc2.Instance = DMA1_Stream1;
        hdma_adc2.Init.Request = DMA_REQUEST_ADC2;
        hdma_adc2.Init.Mode = DMA_CIRCULAR;
        hdma_adc2.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_adc2.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_adc2.Init.MemInc = DMA_MINC_ENABLE;
        hdma_adc2.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
        hdma_adc2.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
        hdma_adc2.Init.Priority = DMA_PRIORITY_VERY_HIGH;
        hdma_adc2.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(&hdma_adc2) != HAL_OK)
        {
            APP_ERR("ADC2 DMA Init failed!");
            Error_Handler();
        }

        __HAL_LINKDMA(adcHandle, DMA_Handle, hdma_adc2);
        
        // 确保中断被正确配置
        HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
        
        // // 启用 ADC 中断
        // HAL_NVIC_SetPriority(ADC_IRQn, 0, 0);
        // HAL_NVIC_EnableIRQ(ADC_IRQn);
        
        APP_DBG("ADC2 DMA configured successfully");
    } 
    else if (adcHandle->Instance == ADC3)
    {
        /* USER CODE BEGIN ADC3_MspInit 0 */

        /* USER CODE END ADC3_MspInit 0 */
        /* ADC3 clock enable */
        __HAL_RCC_ADC3_CLK_ENABLE();

        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOH_CLK_ENABLE();
        __HAL_RCC_GPIOF_CLK_ENABLE();
        /**ADC3 GPIO Configuration
        PF5     ------> ADC3_INP4
        PF3     ------> ADC3_INP5
        PF4     ------> ADC3_INP9
        PC2     ------> ADC3_INP12
        PH2     ------> ADC3_INP13
        */

        GPIO_InitStruct.Pin = GPIO_PIN_2;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_2;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

        /* ADC3 DMA Init */
        /* ADC3 Init */
        hdma_adc3.Instance = BDMA_Channel0;
        hdma_adc3.Init.Request = BDMA_REQUEST_ADC3;
        hdma_adc3.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_adc3.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_adc3.Init.MemInc = DMA_MINC_ENABLE;
        hdma_adc3.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
        hdma_adc3.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
        hdma_adc3.Init.Mode = DMA_CIRCULAR;
        hdma_adc3.Init.Priority = DMA_PRIORITY_VERY_HIGH;
        hdma_adc3.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(&hdma_adc3) != HAL_OK)
        {
            Error_Handler();
        }

        __HAL_LINKDMA(adcHandle, DMA_Handle, hdma_adc3);

        /* USER CODE BEGIN ADC3_MspInit 1 */
        // ADC3 BDMA 中断配置
        HAL_NVIC_SetPriority(BDMA_Channel0_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(BDMA_Channel0_IRQn);
        /* USER CODE END ADC3_MspInit 1 */
    }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *adcHandle)
{

    if (adcHandle->Instance == ADC1)
    {
        /* USER CODE BEGIN ADC1_MspDeInit 0 */

        /* USER CODE END ADC1_MspDeInit 0 */
        /* Peripheral clock disable */
        HAL_RCC_ADC12_CLK_ENABLED--;
        if (HAL_RCC_ADC12_CLK_ENABLED == 0)
        {
            __HAL_RCC_ADC12_CLK_DISABLE();
        }

        /**ADC1 GPIO Configuration
        PF11     ------> ADC1_INP2
        PA6     ------> ADC1_INP3
        PC4     ------> ADC1_INP4
        PB1     ------> ADC1_INP5
        PF12     ------> ADC1_INP6
        PA7     ------> ADC1_INP7
        */

        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_6 | GPIO_PIN_7);

        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_4);

        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_1);

        HAL_GPIO_DeInit(GPIOF, GPIO_PIN_11 | GPIO_PIN_12);

        /* ADC1 DMA DeInit */
        HAL_DMA_DeInit(adcHandle->DMA_Handle);
        /* USER CODE BEGIN ADC1_MspDeInit 1 */

        /* USER CODE END ADC1_MspDeInit 1 */
    }
    else if (adcHandle->Instance == ADC2)
    {
        /* USER CODE BEGIN ADC2_MspDeInit 0 */

        /* USER CODE END ADC2_MspDeInit 0 */
        /* Peripheral clock disable */
        HAL_RCC_ADC12_CLK_ENABLED--;
        if (HAL_RCC_ADC12_CLK_ENABLED == 0)
        {
            __HAL_RCC_ADC12_CLK_DISABLE();
        }

        /**ADC2 GPIO Configuration
        PF13     ------> ADC2_INP2
        PF14     ------> ADC2_INP6
        PC5     ------> ADC2_INP8
        PB0     ------> ADC2_INP9
        PC0     ------> ADC2_INP10
        PC1     ------> ADC2_INP11
        */
        HAL_GPIO_DeInit(GPIOB, GPIO_PIN_0);

        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_5);

        HAL_GPIO_DeInit(GPIOF, GPIO_PIN_13 | GPIO_PIN_14);

        /* ADC2 DMA DeInit */
        HAL_DMA_DeInit(adcHandle->DMA_Handle);
        /* USER CODE BEGIN ADC2_MspDeInit 1 */

        /* USER CODE END ADC2_MspDeInit 1 */
    } 
    else if (adcHandle->Instance == ADC3)
    {
        /* USER CODE BEGIN ADC3_MspDeInit 0 */

        /* USER CODE END ADC3_MspDeInit 0 */
        /* Peripheral clock disable */
        __HAL_RCC_ADC3_CLK_DISABLE();

        /**ADC3 GPIO Configuration
        PF5     ------> ADC3_INP4
        PF3     ------> ADC3_INP5
        PF4     ------> ADC3_INP9
        PC2     ------> ADC3_INP12
        PH2     ------> ADC3_INP13
        */

        HAL_GPIO_DeInit(GPIOC, GPIO_PIN_2);

        HAL_GPIO_DeInit(GPIOH, GPIO_PIN_2);

        HAL_GPIO_DeInit(GPIOF, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);

        /* ADC3 DMA DeInit */
        HAL_DMA_DeInit(adcHandle->DMA_Handle);
        /* USER CODE BEGIN ADC3_MspDeInit 1 */

        /* USER CODE END ADC3_MspDeInit 1 */
    }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
