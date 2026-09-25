#include "system_sleep_manager.hpp"
#include "auto_sleep_policy.hpp"
#include "board_cfg.h"
#include "board_power.hpp"
#include "board_mode.hpp"
#include "states/input_state.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "leds/leds_manager.hpp"
#include "storagemanager.hpp"
#include "connection_manager.hpp"
#include "rf_bridge_port.hpp"
#include "rotary-encoder.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"
#include "system_logger.h"

namespace {
using State = AutoSleepPolicy::State;
constexpr uint32_t rotaryMask = 1u << 31;
AutoSleepPolicy policy;
SleepReleaseGate releaseGate;
SleepWakeKeys wakeKeys; // ISR-owned; main reads only with interrupts masked.
volatile bool keysReady = false;
bool resetInhibit = false;
bool userEnabled = false;
uint32_t configuredTimeout = 0u;
bool idleContextValid = false;
bool rotarySuppressed = false;
bool inputPaused = false;
bool ledsStopped = false;
bool samplingResumed = false;
uint32_t lastInput = 0u, lastKeys = 0u, lastKeepalive = 0u;
uint32_t samplingResumeAt = 0u;
uint32_t lastServiceMs = 0u;

uint32_t rawKeys()
{
    uint32_t mask = 0;
    if (HAL_GPIO_ReadPin(GPIO_BTN1_PORT, GPIO_BTN1_PIN) == GPIO_PIN_RESET) mask |= 1u << GPIO_BTN1_VIRTUAL_PIN;
    if (HAL_GPIO_ReadPin(GPIO_BTN2_PORT, GPIO_BTN2_PIN) == GPIO_PIN_RESET) mask |= 1u << GPIO_BTN2_VIRTUAL_PIN;
    if (HAL_GPIO_ReadPin(GPIO_BTN3_PORT, GPIO_BTN3_PIN) == GPIO_PIN_RESET) mask |= 1u << GPIO_BTN3_VIRTUAL_PIN;
    if (HAL_GPIO_ReadPin(GPIO_BTN4_PORT, GPIO_BTN4_PIN) == GPIO_PIN_RESET) mask |= 1u << GPIO_BTN4_VIRTUAL_PIN;
    if (HAL_GPIO_ReadPin(ROTENC_BTN_PORT, ROTENC_BTN_PIN) == GPIO_PIN_RESET) mask |= rotaryMask;
    return mask;
}

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


void beginRestore(uint32_t now, uint32_t keys)
{
    releaseGate.arm(keys & ~rotaryMask);
    rotarySuppressed = true;
    if (!inputPaused) { policy.active(now); return; }
    BOARD_POWER.setHallEnabled(true);
    samplingResumed = false;
    policy.transition(State::Restoring, now);
    APP_STAGE("S94", "auto sleep restoring input; wake mask=%08lx", (unsigned long)keys);
}

void fail(uint32_t now, const char* reason)
{
    policy.inhibit();
    APP_STAGE_ERROR("S96", "auto sleep disabled for this boot: %s", reason);
    if (policy.state() == State::Restoring) {
        INPUT_STATE.failSleepResume();
        inputPaused = ledsStopped = false;
        policy.active(now);
        SPIScreenManager::getInstance().resumeFromSleep();
    } else {
        beginRestore(now, rawKeys());
    }
}
} // namespace

extern "C" void SystemSleep_CaptureBootFlags(void)
{
    const uint32_t flags = RCC->RSR;
    // POR/BOR also assert PIN/CPU reset flags. Do not classify cold power-up
    // as a manual reset, but always fail closed for explicit fault sources.
    const bool cold = (flags & (RCC_RSR_PORRSTF | RCC_RSR_BORRSTF)) != 0u;
    const bool software = (flags & RCC_RSR_SFTRSTF) != 0u;
    resetInhibit = (flags & (RCC_RSR_IWDG1RSTF |
        RCC_RSR_WWDG1RSTF | RCC_RSR_LPWRRSTF)) != 0u ||
        (!cold && !software && (flags & (RCC_RSR_PINRSTF | RCC_RSR_CPURSTF)) != 0u);
    forceRunPowerPolicy();
}

extern "C" void SystemSleep_ConfirmWakeHoldOrReturnStandby(void)
{
    // Legacy entry point: ALWAYS continue boot. Never return to Standby.
    forceRunPowerPolicy();
    policy.initialize(HAL_GetTick(), HBOX_AUTO_SLEEP_ENABLED != 0 && !resetInhibit);
}

extern "C" void SystemSleep_InitializeWakeKeys(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = GPIO_BTN1_PIN | GPIO_BTN2_PIN | GPIO_BTN3_PIN | GPIO_BTN4_PIN;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = ROTENC_BTN_PIN;
    HAL_GPIO_Init(ROTENC_BTN_PORT, &gpio);
    uint32_t held = rawKeys();
    for (unsigned i = 0; i < 20; ++i) { HAL_Delay(1u); held &= rawKeys(); }
    if (held != 0u) {
        policy.inhibit();
        releaseGate.arm(held & ~rotaryMask);
        rotarySuppressed = true;
    }
    keysReady = true;
}

extern "C" void SystemSleep_Tick1msFromISR(void)
{
    if (keysReady) wakeKeys.sample(rawKeys());
}

extern "C" void SystemSleep_HandleWakeRecovery(void) { forceRunPowerPolicy(); }
extern "C" void SystemSleep_RequestStandby(void)
{
    // Low-battery / old manual deep-sleep requests remain disabled.
    APP_STAGE("S00D", "STM32 deep Standby request ignored by global policy");
}
extern "C" void SystemSleep_DisableForBoot(void) { policy.inhibit(); }
extern "C" bool SystemSleep_IsBusy(void) { return policy.state() != State::Active; }
extern "C" uint32_t SystemSleep_FilterInput(uint32_t mask)
{
    // Collect held Hall keys across the initial debounce samples, not merely
    // the first DMA frame (which can precede a pressed-state transition).
    if (policy.state() == State::Restoring) {
        releaseGate.arm(mask);
        (void)releaseGate.filter(mask);
        return 0u;
    }
    return releaseGate.filter(mask);
}

extern "C" void SystemSleep_NotifyButtonActivity(uint32_t now, uint32_t mask)
{
    if (mask != 0u || mask != lastInput) policy.activity(now);
    lastInput = mask;
}
extern "C" void SystemSleep_NotifyScreenActivity(uint32_t now) { policy.activity(now); }
extern "C" void SystemSleep_UpdateRotaryHold(uint32_t now)
{
    if (RotEnc_IsButtonDown()) policy.activity(now);
}
extern "C" void SystemSleep_UpdateAutoStandby(uint32_t) {}

extern "C" bool SystemSleep_ShouldSuppressRotaryLongAction(void)
{
    return SystemSleep_IsBusy() || rotarySuppressed;
}

extern "C" void SystemSleep_CancelForModeChange(void)
{
    if (SystemSleep_IsBusy()) {
        // The caller tears down/restarts the old input owner itself.
        releaseGate.arm(rawKeys() & ~rotaryMask);
        rotarySuppressed = true;
        SPIScreenManager::getInstance().resumeFromSleep();
    }
    inputPaused = ledsStopped = samplingResumed = false;
    policy.active(HAL_GetTick());
}

extern "C" void SystemSleep_Service(bool inputMode, bool resetPending)
{
    idleContextValid = inputMode && !resetPending && BOARD_MODE.isStable();
    const uint32_t now = HAL_GetTick();
    const bool enabled = STORAGE_MANAGER.getAutoSleepEnabled();
    const uint32_t timeout = STORAGE_MANAGER.getAutoStandbyMs();
    if (enabled != userEnabled || timeout != configuredTimeout) {
        userEnabled = enabled;
        configuredTimeout = timeout;
        policy.activity(now);
    }
    // User choice never resets the boot/fault inhibitor. Restore asynchronously
    // even if disabled halfway through peripheral preparation or recovery.
    if ((!userEnabled || !policy.enabled()) &&
        (policy.state() == State::Preparing || policy.state() == State::Sleeping)) {
        beginRestore(now, rawKeys());
    }
    if ((!policy.enabled() || !userEnabled) && policy.state() == State::Active && !rotarySuppressed) return;
    if (policy.state() == State::Active && now == lastServiceMs) return;
    lastServiceMs = now;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t keys = wakeKeys.stable();
    const uint32_t pressed = wakeKeys.takePressed();
    __set_PRIMASK(primask);
    if (keys != 0u || keys != lastKeys || pressed != 0u) policy.activity(now);
    lastKeys = keys;

    if (SystemSleep_IsBusy() || rotarySuppressed) {
        (void)RotEnc_GetDetentDelta();
        (void)RotEnc_GetDelta();
        (void)RotEnc_WasButtonPressed();
        (void)RotEnc_WasButtonReleased();
        (void)RotEnc_WasButtonClicked();
        (void)RotEnc_WasButtonLongPressed();
        if (!SystemSleep_IsBusy() && !RotEnc_IsButtonDown() &&
            (rawKeys() & rotaryMask) == 0u) rotarySuppressed = false;
    }
    if (!inputMode || resetPending) {
        policy.activity(now);
        return;
    }

    if (policy.state() == State::Active) {
        const bool eligible = userEnabled && INPUT_STATE.canAutoSleep() && keys == 0u &&
            rawKeys() == 0u && lastInput == 0u && !releaseGate.pending() &&
            SPIScreenManager::getInstance().canAutoSleep();
        if (!policy.shouldPrepare(now, configuredTimeout, eligible)) return;
        inputPaused = ledsStopped = samplingResumed = false;
        policy.transition(State::Preparing, now);
        APP_STAGE("S91", "auto sleep preparing");
    }

    if (policy.timedOut(now)) { fail(now, "stage timeout"); return; }
    const bool enteringActivity = policy.state() == State::Preparing &&
        (rawKeys() != 0u || lastInput != 0u);
    if ((policy.state() == State::Preparing || policy.state() == State::Sleeping) &&
        (pressed != 0u || keys != 0u || enteringActivity || !BOARD_MODE.isStable())) {
        beginRestore(now, keys | pressed | rawKeys());
    }

    if (inputPaused && !samplingResumed && now - lastKeepalive >= 10u) {
        lastKeepalive = now;
        if (!INPUT_STATE.sendSleepNeutral()) { fail(now, "neutral keepalive failed"); return; }
    }
    auto& screen = SPIScreenManager::getInstance();
    if (policy.state() == State::Preparing) {
        if (!inputPaused) {
            if (!INPUT_STATE.sendSleepNeutral()) { fail(now, "neutral submit failed"); return; }
            lastKeepalive = now;
            inputPaused = true;
            if (!INPUT_STATE.pauseForSleep()) { fail(now, "ADC stop failed"); return; }
        }
        // RF owns an asynchronous latest-input queue; let neutral drain first.
        if (!RFBridgePort_IsInputIdle()) return;
        if (!ledsStopped) {
            ledsStopped = true;
#if HAS_LED == 1
            if (!LEDS_MANAGER.suspendForSleep()) { fail(now, "LED stop failed"); return; }
#endif
        }
        if (!screen.suspendForSleep()) return;
        policy.transition(State::Sleeping, now);
        APP_STAGE("S92", "auto sleep active; main/QSPI/CH585 retained");
    } else if (policy.state() == State::Restoring) {
        if (!samplingResumed && policy.elapsed(now) >= BOARD_HALL_STABILIZE_MS) {
            releaseGate.arm(keys & ~rotaryMask);
            if (!INPUT_STATE.resumeFromSleep()) { fail(now, "ADC restart failed"); return; }
            samplingResumed = true;
            samplingResumeAt = HAL_GetTick();
        }
        if (samplingResumed && INPUT_STATE.sleepInputReady() && now - samplingResumeAt >= 10u) {
            APP_STAGE("S94", "input restored after %lu ms", (unsigned long)policy.elapsed(now));
            INPUT_STATE.finishSleepResume();
            inputPaused = ledsStopped = false;
            policy.active(now);
            screen.resumeFromSleep();
        }
    }
}

extern "C" void SystemSleep_Idle(void)
{
#if HBOX_AUTO_SLEEP_ENABLED == 1
    // CPU-only shallow sleep. Peripheral preparation/recovery belongs to Service.
    // SysTick and communications remain live; a tick bounds a raced key to 1 ms.
    if (!idleContextValid || !policy.enabled() || !userEnabled ||
        !STORAGE_MANAGER.getAutoSleepEnabled() || policy.state() != State::Sleeping ||
        !BOARD_MODE.isStable() || rawKeys() != 0u || __get_PRIMASK() != 0u ||
        (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0u) return;
    CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
    __DSB();
    __WFI();
    __ISB();
#endif
}
