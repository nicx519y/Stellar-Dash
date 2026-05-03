#include "report_scheduler.hpp"

extern "C" {
#include "tim.h"
}

#include "board_cfg.h"

namespace {
static uint16_t clamp_rate(uint16_t rateHz) {
    switch (rateHz) {
        case 1000:
        case 2000:
        case 4000:
        case 8000:
            return rateHz;
        default:
            return 1000;
    }
}

static uint32_t get_tim2_clock_hz() {
    uint32_t pclk = HAL_RCC_GetPCLK1Freq();
    const uint32_t ppre = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1);
    if (ppre != RCC_HCLK_DIV1) {
        return pclk * 2u;
    }
    return pclk;
}
}

void ReportScheduler::setRate(uint16_t rateHz) {
    runningRateHz = clamp_rate(rateHz);

    const uint32_t timClkHz = get_tim2_clock_hz();
    const uint32_t counterHz = 1000000u;
    uint32_t prescaler = (timClkHz / counterHz);
    if (prescaler == 0u) prescaler = 1u;
    prescaler -= 1u;

    uint32_t period = (counterHz / runningRateHz);
    if (period == 0u) period = 1u;
    period -= 1u;

    __HAL_TIM_DISABLE_IT(&htim2, TIM_IT_UPDATE);
    HAL_TIM_Base_Stop_IT(&htim2);
    __HAL_TIM_SET_PRESCALER(&htim2, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim2, period);
    __HAL_TIM_SET_COUNTER(&htim2, 0u);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    HAL_TIM_Base_Start_IT(&htim2);
}

void ReportScheduler::start(uint16_t rateHz) {
    pendingTicks = 0;
    setRate(rateHz);
    started = true;
}

void ReportScheduler::stop() {
    __HAL_TIM_DISABLE_IT(&htim2, TIM_IT_UPDATE);
    HAL_TIM_Base_Stop_IT(&htim2);
    pendingTicks = 0;
    started = false;
}

void ReportScheduler::onTimerIrq() {
    if (!started) return;
    if (pendingTicks < 8u) {
        pendingTicks++;
    }
}

bool ReportScheduler::consumeTick() {
    if (!started || pendingTicks == 0u) return false;
    __disable_irq();
    bool hasTick = (pendingTicks > 0u);
    if (hasTick) {
        pendingTicks--;
    }
    __enable_irq();
    return hasTick;
}

extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim && htim->Instance == BOARD_TIM2_INSTANCE) {
        REPORT_SCHEDULER.onTimerIrq();
    }
}
