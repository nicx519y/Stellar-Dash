#include "system_sleep_manager.hpp"

#include "board_cfg.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"
#include "system_logger.h"

/*
 * Deep Standby is intentionally disabled for the entire product runtime.
 * Keep this policy centralized: callers may continue reporting activity or
 * requesting sleep, but no state can arm wake pins, set PDDS, execute a deep
 * sleep instruction, or return a retained wake back to Standby. LCD screen
 * standby remains an independent UI feature in SPIScreenManager.
 */

namespace {

static uint32_t s_lastActivityMs = 0u;

static void forceRunPowerPolicy()
{
    HAL_PWREx_DisableWakeUpPin(PWR_WAKEUP_PIN1);
    CLEAR_BIT(PWR->CPUCR,
              PWR_CPUCR_PDDS_D1 | PWR_CPUCR_PDDS_D2 | PWR_CPUCR_PDDS_D3);
    SET_BIT(PWR->CPUCR, PWR_CPUCR_RUN_D3);
    CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
    (void)HAL_PWREx_ClearWakeupFlag(PWR_WAKEUP_FLAG_ALL);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    __DSB();
    __ISB();
}

} // namespace

extern "C" void SystemSleep_CaptureBootFlags(void)
{
    forceRunPowerPolicy();
}

extern "C" void SystemSleep_ConfirmWakeHoldOrReturnStandby(void)
{
    forceRunPowerPolicy();
    s_lastActivityMs = HAL_GetTick();
}

extern "C" void SystemSleep_HandleWakeRecovery(void)
{
    forceRunPowerPolicy();
}

extern "C" void SystemSleep_RequestStandby(void)
{
    s_lastActivityMs = HAL_GetTick();
    APP_STAGE("S00D", "STM32 deep Standby request ignored by global policy");
}

extern "C" void SystemSleep_UpdateRotaryHold(uint32_t nowMs)
{
    s_lastActivityMs = nowMs;
}

extern "C" void SystemSleep_NotifyButtonActivity(uint32_t nowMs,
                                                   uint32_t inputMask)
{
    if (inputMask != 0u) s_lastActivityMs = nowMs;
}

extern "C" void SystemSleep_NotifyScreenActivity(uint32_t nowMs)
{
    s_lastActivityMs = nowMs;
}

extern "C" void SystemSleep_UpdateAutoStandby(uint32_t nowMs)
{
    /* LCD standby is serviced elsewhere; STM32 deep Standby stays disabled. */
    s_lastActivityMs = nowMs;
}

extern "C" bool SystemSleep_ShouldSuppressRotaryLongAction(void)
{
    return false;
}
