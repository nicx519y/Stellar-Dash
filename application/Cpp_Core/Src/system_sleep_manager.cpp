#include "system_sleep_manager.hpp"

#include "adc.h"
#include "board_cfg.h"
#include "board_mode.hpp"
#include "ch585_role_bootstrap.hpp"
#include "connection_manager.hpp"
#include "leds/leds_manager.hpp"
#include "report_scheduler.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "spi-st7789.h"
#include "storagemanager.hpp"
#include "system_logger.h"
#include "usb_board_link.hpp"
#include "usbhostmanager.hpp"
#include "board_power.hpp"

#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"

#ifndef SYSTEM_SLEEP_ENTER_HOLD_MS
#define SYSTEM_SLEEP_ENTER_HOLD_MS 5000u
#endif

#ifndef SYSTEM_SLEEP_WAKE_HOLD_MS
#define SYSTEM_SLEEP_WAKE_HOLD_MS 3000u
#endif

#ifndef SYSTEM_SLEEP_AUTO_STANDBY_NONE_MS
#define SYSTEM_SLEEP_AUTO_STANDBY_NONE_MS 0u
#endif

#ifndef SYSTEM_SLEEP_AUTO_STANDBY_DEFAULT_MS
#define SYSTEM_SLEEP_AUTO_STANDBY_DEFAULT_MS 300000u
#endif

#ifndef SYSTEM_SLEEP_WKUP1_PULL
#define SYSTEM_SLEEP_WKUP1_PULL PWR_PIN_PULL_UP
#endif

#ifndef SYSTEM_SLEEP_RELEASE_DEBOUNCE_MS
#define SYSTEM_SLEEP_RELEASE_DEBOUNCE_MS 50u
#endif

#ifndef SYSTEM_SLEEP_BKP_INDEX
#define SYSTEM_SLEEP_BKP_INDEX 15u
#endif

#ifndef SYSTEM_SLEEP_BKP_WAKE_HOLD_INDEX
#define SYSTEM_SLEEP_BKP_WAKE_HOLD_INDEX 14u
#endif

#define SYSTEM_SLEEP_BKP_ENTRY_HELD 0x48534C50u

namespace {

static bool s_bootFlagsCaptured = false;
static bool s_bootedFromStandby = false;
static bool s_wakeupFromPa0 = false;
static bool s_wakeHoldConfirmed = false;
static bool s_waitPa0Release = false;
static bool s_sleepEntering = false;
static bool s_rotaryHoldActive = false;
static uint32_t s_rotaryHoldStartMs = 0;
static uint32_t s_lastActivityMs = 0;

static void reset_runtime_hold()
{
    s_rotaryHoldActive = false;
    s_rotaryHoldStartMs = 0;
}

static bool is_pa0_down()
{
    return HAL_GPIO_ReadPin(ROTENC_BTN_PORT, ROTENC_BTN_PIN) == GPIO_PIN_RESET;
}

static void bkp_write(uint32_t idx, uint32_t val)
{
    HAL_PWR_EnableBkUpAccess();
    volatile uint32_t* base = &RTC->BKP0R;
    base[idx] = val;
}

static uint32_t bkp_read(uint32_t idx)
{
    volatile uint32_t* base = &RTC->BKP0R;
    return base[idx];
}

static bool standby_entry_had_pa0_held()
{
    return bkp_read(SYSTEM_SLEEP_BKP_INDEX) == SYSTEM_SLEEP_BKP_ENTRY_HELD;
}

static uint32_t sanitize_wake_hold_ms(uint32_t value)
{
    if (value < 1000u || value > 5000u) {
        return SYSTEM_SLEEP_WAKE_HOLD_MS;
    }
    return (value / 1000u) * 1000u;
}

static uint32_t sanitize_auto_standby_ms(uint32_t value)
{
    switch (value) {
        case 10000u:
        case 30000u:
        case 60000u:
        case 120000u:
        case 300000u:
            return value;
        default:
            return SYSTEM_SLEEP_AUTO_STANDBY_DEFAULT_MS;
    }
}

static void mark_standby_entry_pa0_state(bool pa0Held)
{
    bkp_write(SYSTEM_SLEEP_BKP_INDEX, pa0Held ? SYSTEM_SLEEP_BKP_ENTRY_HELD : 0u);
}

static uint32_t wake_hold_ms_from_backup()
{
    return sanitize_wake_hold_ms(bkp_read(SYSTEM_SLEEP_BKP_WAKE_HOLD_INDEX));
}

static void mark_wake_hold_ms_for_next_boot(uint32_t wakeHoldMs)
{
    bkp_write(SYSTEM_SLEEP_BKP_WAKE_HOLD_INDEX, sanitize_wake_hold_ms(wakeHoldMs));
}

static void init_pa0_input_for_hold_check()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef init = {0};
    init.Pin = ROTENC_BTN_PIN;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ROTENC_BTN_PORT, &init);
}

static void configure_wkup1_for_standby()
{
    init_pa0_input_for_hold_check();
    HAL_PWREx_DisableWakeUpPin(PWR_WAKEUP_PIN1);
    (void)HAL_PWREx_ClearWakeupFlag(PWR_WAKEUP_FLAG_ALL);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);

    PWREx_WakeupPinTypeDef wakePin = {0};
    wakePin.WakeUpPin = PWR_WAKEUP_PIN1;
    wakePin.PinPolarity = PWR_PIN_POLARITY_LOW;
    wakePin.PinPull = SYSTEM_SLEEP_WKUP1_PULL;
    HAL_PWREx_EnableWakeUpPin(&wakePin);
}

static void wait_pa0_release_for_rearm()
{
    if (!is_pa0_down()) {
        return;
    }

    LOG_INFO("SYSTEM_SLEEP", "Waiting PA0 release before re-arming Standby");
    Logger_Flush();
    while (is_pa0_down()) {
        HAL_Delay(5u);
    }
    HAL_Delay(SYSTEM_SLEEP_RELEASE_DEBOUNCE_MS);
}

static void stop_adcs()
{
    (void)HAL_ADC_Stop_DMA(&hadc1);
    (void)HAL_ADC_Stop_DMA(&hadc2);
    (void)HAL_ADC_Stop_DMA(&hadc3);
    (void)HAL_ADC_Stop(&hadc1);
    (void)HAL_ADC_Stop(&hadc2);
    (void)HAL_ADC_Stop(&hadc3);
}

static void stop_screen()
{
    SPIScreenManager::getInstance().shutdown();
}

static void stop_usb()
{
    USB_HOST_MANAGER.shutdown();
    USB_BOARD_LINK.shutdown();
}

[[noreturn]] static void enter_standby_now()
{
    init_pa0_input_for_hold_check();
    mark_standby_entry_pa0_state(is_pa0_down());
    configure_wkup1_for_standby();
    HAL_SuspendTick();
    HAL_PWR_EnterSTANDBYMode();
    NVIC_SystemReset();
    while (true) {
    }
}

static void request_standby()
{
#if WEBCONFIG_TEST_FORCE_BOOT
    /*
     * Bench/WebConfig bring-up must remain reachable through SWD and keep the
     * local display alive.  Disable the common Standby sink itself so timeout,
     * low-voltage and explicit/manual callers cannot bypass the individual
     * auto-standby guard while this temporary debug build is active.
     */
    s_sleepEntering = false;
    s_lastActivityMs = HAL_GetTick();
    LOG_INFO("SYSTEM_SLEEP", "Standby request ignored by WebConfig bring-up override");
    return;
#endif

    if (s_sleepEntering) {
        return;
    }
    s_sleepEntering = true;

    LOG_INFO("SYSTEM_SLEEP", "Entering Standby");
    Logger_Flush();

    REPORT_SCHEDULER.stop();
    stop_adcs();
    stop_screen();
#if HAS_LED == 1
    LEDS_MANAGER.deinit();
#endif
    stop_usb();

    reset_runtime_hold();
    mark_wake_hold_ms_for_next_boot(STORAGE_MANAGER.getWakeHoldMs());
    if (BOARD_MODE.isStable() &&
        BOARD_MODE.current() == BoardMode::Rf &&
        CH585_ROLE_BOOTSTRAP.isLocked() &&
        CH585_ROLE_BOOTSTRAP.role() == Ch585Role::Rf &&
        !CONNECTION_MANAGER.ensureRfSleeping(RfPowerReason::SystemSleep)) {
        LOG_ERROR("SYSTEM_SLEEP", "CH585 RF sleep command failed; entering Standby anyway");
    }
    CH585_ROLE_BOOTSTRAP.shutdown();
    BOARD_POWER.prepareForStandby();
    Logger_Flush();

    enter_standby_now();
}

}

extern "C" void SystemSleep_CaptureBootFlags(void)
{
    s_bootedFromStandby = (__HAL_PWR_GET_FLAG(PWR_FLAG_SB) != 0U);
    s_wakeupFromPa0 = (HAL_PWREx_GetWakeupFlag(PWR_WAKEUP_FLAG1) != 0U);
    s_bootFlagsCaptured = true;
}

extern "C" void SystemSleep_ConfirmWakeHoldOrReturnStandby(void)
{
    if (!s_bootFlagsCaptured) {
        SystemSleep_CaptureBootFlags();
    }

#if WEBCONFIG_TEST_FORCE_BOOT
    /*
     * Bring-up firmware must never disappear back into Standby before USART
     * and the recovery display are initialized.  A debugger reset does not
     * necessarily clear the retained Standby/WKUP flags, so treating those
     * flags as a fresh low-power wake can immediately put the target back to
     * sleep and make both the screen and SWD appear dead.  Clear the retained
     * wake state and continue with a normal cold-start path instead.
     */
    /*
     * HAL_PWR_EnterSTANDBYMode() selects DSTANDBY for every power domain by
     * setting PDDS_D1/D2/D3 before executing WFI.  A debug/system reset can
     * leave that power-policy state and the wake pin armed even though the CPU
     * has restarted.  Clearing only SB/WKUP therefore is not a complete
     * "Standby disabled" override: a later deep-sleep instruction can send the
     * board straight back down.
     *
     * Keep D3 alive and force every domain back to the non-Standby selection
     * for the temporary WebConfig bench image.  This changes runtime registers
     * only; it does not touch Option Bytes or any protection state.
     */
    HAL_PWREx_DisableWakeUpPin(PWR_WAKEUP_PIN1);
    CLEAR_BIT(PWR->CPUCR,
              PWR_CPUCR_PDDS_D1 | PWR_CPUCR_PDDS_D2 | PWR_CPUCR_PDDS_D3);
    SET_BIT(PWR->CPUCR, PWR_CPUCR_RUN_D3);
    CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
    __DSB();
    __ISB();

    s_bootedFromStandby = false;
    s_wakeupFromPa0 = false;
    s_wakeHoldConfirmed = false;
    s_waitPa0Release = false;
    mark_standby_entry_pa0_state(false);
    (void)HAL_PWREx_ClearWakeupFlag(PWR_WAKEUP_FLAG_ALL);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    return;
#endif

    if (!s_bootedFromStandby || !s_wakeupFromPa0) {
        mark_standby_entry_pa0_state(false);
        (void)HAL_PWREx_ClearWakeupFlag(PWR_WAKEUP_FLAG_ALL);
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
        return;
    }

    init_pa0_input_for_hold_check();

    if (standby_entry_had_pa0_held()) {
        mark_standby_entry_pa0_state(false);
        LOG_INFO("SYSTEM_SLEEP", "Ignoring wake from sleep-entry PA0 hold");
        wait_pa0_release_for_rearm();
        enter_standby_now();
    }

    const uint32_t wakeHoldMs = wake_hold_ms_from_backup();
    const uint32_t start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < wakeHoldMs) {
        if (!is_pa0_down()) {
            mark_standby_entry_pa0_state(false);
            enter_standby_now();
        }
        HAL_Delay(5u);
    }

    s_wakeHoldConfirmed = true;
    s_waitPa0Release = true;
    s_lastActivityMs = HAL_GetTick();
    reset_runtime_hold();
    mark_standby_entry_pa0_state(false);
    (void)HAL_PWREx_ClearWakeupFlag(PWR_WAKEUP_FLAG_ALL);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
}

extern "C" void SystemSleep_HandleWakeRecovery(void)
{
    if (!s_wakeHoldConfirmed) {
        return;
    }

    LOG_INFO("SYSTEM_SLEEP", "Standby wake confirmed, restoring runtime");

    /*
     * Standby resets the STM32 and PI10 is shut down before entry. The active
     * state has already cold-bootstrapped CH585 from the physical switch by
     * the time this hook runs, so never replay a persisted RF wake command
     * into a USB-locked CH585.
     */
    if (!BOARD_MODE.isStable() ||
        (BOARD_MODE.current() != BoardMode::Rf &&
         BOARD_MODE.current() != BoardMode::Usb)) {
        CH585_ROLE_BOOTSTRAP.shutdown();
        BOARD_POWER.enterSafeState();
    }
}

extern "C" void SystemSleep_RequestStandby(void)
{
    request_standby();
}

extern "C" void SystemSleep_UpdateRotaryHold(uint32_t nowMs)
{
    if (s_sleepEntering) {
        return;
    }

    if (s_waitPa0Release) {
        if (!is_pa0_down()) {
            s_waitPa0Release = false;
            reset_runtime_hold();
        }
        return;
    }

    if (STORAGE_MANAGER.getBootMode() != BootMode::BOOT_MODE_INPUT) {
        reset_runtime_hold();
        return;
    }

    if (!is_pa0_down()) {
        reset_runtime_hold();
        return;
    }

    if (!s_rotaryHoldActive) {
        s_rotaryHoldActive = true;
        s_rotaryHoldStartMs = nowMs;
        return;
    }

    if ((uint32_t)(nowMs - s_rotaryHoldStartMs) >= SYSTEM_SLEEP_ENTER_HOLD_MS) {
        request_standby();
    }
}

extern "C" void SystemSleep_NotifyButtonActivity(uint32_t nowMs, uint32_t inputMask)
{
    if (inputMask != 0u) {
        s_lastActivityMs = nowMs;
    }
}

extern "C" void SystemSleep_NotifyScreenActivity(uint32_t nowMs)
{
    s_lastActivityMs = nowMs;
}

extern "C" void SystemSleep_UpdateAutoStandby(uint32_t nowMs)
{
    if (s_sleepEntering) {
        return;
    }

#if WEBCONFIG_TEST_FORCE_BOOT
    /*
     * The temporary WebConfig bring-up override does not persist its runtime
     * boot mode.  Keep the inactivity timer disarmed while that override is
     * active so a long browser/debug session cannot enter Standby underneath
     * an attached debugger.  Explicit/manual Standby requests remain enabled.
     */
    s_lastActivityMs = nowMs;
    return;
#endif

    if (STORAGE_MANAGER.getBootMode() != BootMode::BOOT_MODE_INPUT) {
        s_lastActivityMs = nowMs;
        return;
    }

    const uint32_t autoStandbyMs = sanitize_auto_standby_ms(STORAGE_MANAGER.getAutoStandbyMs());
    if (autoStandbyMs == SYSTEM_SLEEP_AUTO_STANDBY_NONE_MS) {
        s_lastActivityMs = nowMs;
        return;
    }

    if (s_waitPa0Release) {
        s_lastActivityMs = nowMs;
        return;
    }

    if (s_lastActivityMs == 0u) {
        s_lastActivityMs = nowMs;
        return;
    }

    if ((uint32_t)(nowMs - s_lastActivityMs) >= autoStandbyMs) {
        LOG_INFO("SYSTEM_SLEEP", "Auto Standby timeout: %lu ms", (unsigned long)autoStandbyMs);
        request_standby();
    }
}

extern "C" bool SystemSleep_ShouldSuppressRotaryLongAction(void)
{
    return s_sleepEntering || s_waitPa0Release;
}
