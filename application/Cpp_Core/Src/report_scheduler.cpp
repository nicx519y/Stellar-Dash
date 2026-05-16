#include "report_scheduler.hpp"

extern "C" {
#include "tim.h"
}

#include "board_cfg.h"
#include "system_logger.h"

namespace {
static constexpr uint32_t kPendingTickLimit = 64u;

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
    irqTicksWin = 0;
    consumedTicksWin = 0;
    droppedTicksWin = 0;
    statLastMs = HAL_GetTick();
    setRate(rateHz);
    started = true;
}

void ReportScheduler::stop() {
    __HAL_TIM_DISABLE_IT(&htim2, TIM_IT_UPDATE);
    HAL_TIM_Base_Stop_IT(&htim2);
    pendingTicks = 0;
    irqTicksWin = 0;
    consumedTicksWin = 0;
    droppedTicksWin = 0;
    started = false;
}

void ReportScheduler::onTimerIrq() {
    if (!started) return;
    irqTicksWin++;
    if (pendingTicks < kPendingTickLimit) {
        pendingTicks++;
    } else {
        droppedTicksWin++;
    }
}

bool ReportScheduler::consumeTick() {
    if (!started || pendingTicks == 0u) return false;
    uint32_t pendingSnapshot = 0u;
    __disable_irq();
    bool hasTick = (pendingTicks > 0u);
    if (hasTick) {
        pendingTicks--;
        consumedTicksWin++;
    }
    pendingSnapshot = pendingTicks;
    __enable_irq();

    const uint32_t nowMs = HAL_GetTick();
    if (hasTick && ((nowMs - statLastMs) >= 5000u)) {
        uint32_t irqWin;
        uint32_t consumedWin;
        uint32_t droppedWin;
        __disable_irq();
        irqWin = irqTicksWin;
        consumedWin = consumedTicksWin;
        droppedWin = droppedTicksWin;
        pendingSnapshot = pendingTicks;
        irqTicksWin = 0u;
        consumedTicksWin = 0u;
        droppedTicksWin = 0u;
        __enable_irq();

        const uint32_t elapsed = nowMs - statLastMs;
        const uint32_t irqHz = (elapsed != 0u) ? ((irqWin * 1000u) / elapsed) : 0u;
        const uint32_t consumedHz = (elapsed != 0u) ? ((consumedWin * 1000u) / elapsed) : 0u;
        APP_DBG("[REPORT_SCHED][5s] irq:%lu consumed:%lu dropped:%lu pending:%lu irq_hz:%lu consumed_hz:%lu rate:%u",
                irqWin,
                consumedWin,
                droppedWin,
                pendingSnapshot,
                irqHz,
                consumedHz,
                runningRateHz);
        statLastMs = nowMs;
    }
    return hasTick;
}

bool ReportScheduler::consumeLatestTick() {
    if (!started || pendingTicks == 0u) return false;
    uint32_t pendingSnapshot = 0u;
    __disable_irq();
    const uint32_t pending = pendingTicks;
    if (pending > 0u) {
        pendingTicks = 0u;
        consumedTicksWin++;
        if (pending > 1u) {
            droppedTicksWin += (pending - 1u);
        }
    }
    pendingSnapshot = pendingTicks;
    __enable_irq();

    const uint32_t nowMs = HAL_GetTick();
    if ((nowMs - statLastMs) >= 5000u) {
        uint32_t irqWin;
        uint32_t consumedWin;
        uint32_t droppedWin;
        __disable_irq();
        irqWin = irqTicksWin;
        consumedWin = consumedTicksWin;
        droppedWin = droppedTicksWin;
        pendingSnapshot = pendingTicks;
        irqTicksWin = 0u;
        consumedTicksWin = 0u;
        droppedTicksWin = 0u;
        __enable_irq();

        const uint32_t elapsed = nowMs - statLastMs;
        const uint32_t irqHz = (elapsed != 0u) ? ((irqWin * 1000u) / elapsed) : 0u;
        const uint32_t consumedHz = (elapsed != 0u) ? ((consumedWin * 1000u) / elapsed) : 0u;
        APP_DBG("[REPORT_SCHED][5s] irq:%lu consumed:%lu dropped:%lu pending:%lu irq_hz:%lu consumed_hz:%lu rate:%u",
                irqWin,
                consumedWin,
                droppedWin,
                pendingSnapshot,
                irqHz,
                consumedHz,
                runningRateHz);
        statLastMs = nowMs;
    }
    return true;
}

extern "C" void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim && htim->Instance == BOARD_TIM2_INSTANCE) {
        REPORT_SCHEDULER.onTimerIrq();
    }
}
