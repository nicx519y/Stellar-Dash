#include "states/battery_protection_state.hpp"
#include "board_cfg.h"
#include "board_power.hpp"
#include "adc_btns/adc_manager.hpp"
#include "leds/leds_manager.hpp"
#include "power_manager.hpp"
#include "report_scheduler.hpp"
#include "system_sleep_manager.hpp"
#include "main_runtime_control.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "rotary-encoder.h"
#include "stm32h7xx_hal.h"
#pragma GCC optimize("Os")

bool BatteryProtectionState::enter()
{
    // The previous state has relinquished USB/RF and synchronous storage.
    // This latch is deliberately separate from BoardPower's charge fault gate.
    BOARD_POWER.setBatteryLoadInhibited(true);
    SystemSleep_CancelForModeChange();
    REPORT_SCHEDULER.stop();
    ADC_MANAGER.forceStopAllSampling();
    loadsStopped = ADC_MANAGER.isSamplingHardwareStopped();
    if (loadsStopped) BOARD_POWER.setHallEnabled(false);
    const bool ledsStopped = LEDS_MANAGER.suspendForSleep();
    loadsStopped = loadsStopped && ledsStopped;
    if (ledsStopped) {
        BOARD_POWER.setKeyLedEnabled(false);
        BOARD_POWER.setAmbientLedEnabled(false);
    }
    BOARD_POWER.setCh585Enabled(false);
    button = {};
    rawDown = stableDown = HAL_GPIO_ReadPin(ROTENC_BTN_PORT, ROTENC_BTN_PIN) == GPIO_PIN_RESET;
    buttonChangedAt = HAL_GetTick();
    SPIScreenManager::getInstance().setBatteryProtection(true);
    return true; // A failed peripheral stop stays protected, never falls into Input.
}

void BatteryProtectionState::tick()
{
    const uint32_t now = HAL_GetTick();
    const bool down = HAL_GPIO_ReadPin(ROTENC_BTN_PORT, ROTENC_BTN_PIN) == GPIO_PIN_RESET;
    if (down != rawDown) { rawDown = down; buttonChangedAt = now; }
    if (now - buttonChangedAt >= 5u && stableDown != rawDown) {
        stableDown = rawDown;
        if (stableDown) SPIScreenManager::getInstance().showBatteryProtectionStatus();
    }
    const bool ready = POWER_MANAGER.getSnapshot().battery_level == BatteryLevel::RecoveryReady;
    if (button.update(ready, stableDown)) MainRuntime_RequestBatteryRecovery();
    // Never replay the inspection/recovery gesture into a menu or long action.
    (void)RotEnc_GetDetentDelta();
    (void)RotEnc_WasButtonClicked();
    (void)RotEnc_WasButtonLongPressed();
}

void BatteryProtectionState::idle()
{
    if (loadsStopped && SPIScreenManager::getInstance().batteryProtectionDisplayIdle())
        (void)SystemSleep_ProtectionStop();
    // If STOP is unavailable the main loop keeps charging/telemetry alive with
    // loads off. Ordinary GPIO wakeups only resume this same protected loop.
}

void BatteryProtectionState::exit()
{
    BOARD_POWER.setBatteryLoadInhibited(false);
    SystemSleep_SuppressHeldInputs();
    SPIScreenManager::getInstance().setBatteryProtection(false);
}
