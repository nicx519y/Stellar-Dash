#include "delay_timer.h"

static TIM_HandleTypeDef htim6;
static void (*timer_callback)(void) = NULL;

void DelayTimer_Init(void) {
    __HAL_RCC_TIM6_CLK_ENABLE();
    
    htim6.Instance = TIM6;
    
    RCC_ClkInitTypeDef clkconfig;
    uint32_t pFLatency;
    HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);
    
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t tim_clock;
    
    if (clkconfig.APB1CLKDivider == RCC_HCLK_DIV1) {
        tim_clock = pclk1;
    } else {
        tim_clock = 2 * pclk1;
    }
    
    uint32_t prescaler = (tim_clock / 1000000) - 1;
    
    htim6.Init.Prescaler = prescaler;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period = 0xFFFF;
    htim6.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    
    HAL_TIM_Base_Init(&htim6);
    
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

void DelayTimer_Start(uint16_t delay_us) {
    if (delay_us == 0) {
        if (timer_callback) {
            timer_callback();
        }
        return;
    }
    
    __HAL_TIM_SET_AUTORELOAD(&htim6, delay_us);
    __HAL_TIM_SET_COUNTER(&htim6, 0);
    __HAL_TIM_CLEAR_IT(&htim6, TIM_IT_UPDATE);
    
    HAL_TIM_Base_Start_IT(&htim6);
}

void DelayTimer_Stop(void) {
    HAL_TIM_Base_Stop_IT(&htim6);
}

void DelayTimer_SetCallback(void (*callback)(void)) {
    timer_callback = callback;
}

void TIM6_DAC_IRQHandler(void) {
    if (__HAL_TIM_GET_FLAG(&htim6, TIM_FLAG_UPDATE) != RESET) {
        if (__HAL_TIM_GET_IT_SOURCE(&htim6, TIM_IT_UPDATE) != RESET) {
            __HAL_TIM_CLEAR_IT(&htim6, TIM_IT_UPDATE);
            
            HAL_TIM_Base_Stop_IT(&htim6);
            
            if (timer_callback) {
                timer_callback();
            }
        }
    }
}
