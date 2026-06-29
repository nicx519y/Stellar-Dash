#include "system_sleep_manager.hpp"

#include "adc.h"
#include "board_cfg.h"
#include "connection_manager.hpp"
#include "leds/leds_manager.hpp"
#include "report_scheduler.hpp"
#include "spi-st7789.h"
#include "storagemanager.hpp"
#include "system_logger.h"
#include "tusb.h"
#include "usb.h"
#include "usbhostmanager.hpp"

#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_pwr_ex.h"

#ifndef SYSTEM_SLEEP_HOLD_MS
#define SYSTEM_SLEEP_HOLD_MS 5000u
#endif

#ifndef SYSTEM_SLEEP_WKUP1_PULL
#define SYSTEM_SLEEP_WKUP1_PULL PWR_PIN_NO_PULL
#endif

namespace {

static bool s_bootFlagsCaptured = false;
static bool s_bootedFromStandby = false;
static bool s_wakeupFromPa0 = false;
static bool s_wakeHoldConfirmed = false;
static bool s_waitPa0Release = false;
static bool s_sleepEntering = false;
static bool s_rotaryHoldActive = false;
static uint32_t s_rotaryHoldStartMs = 0;

static bool is_pa0_down()
{
    return HAL_GPIO_ReadPin(ROTENC_BTN_PORT, ROTENC_BTN_PIN) == GPIO_PIN_RESET;
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
    (void)SPIST7789_WaitDone(200u);
    SPIST7789_SetBacklight(0u);
    (void)SPIST7789_WaitDone(50u);
}

static void stop_usb()
{
    if (tud_inited()) {
        (void)tud_disconnect();
        for (uint8_t i = 0; i < 10u; ++i) {
            tud_task();
            HAL_Delay(1u);
        }
        (void)tud_deinit(TUD_OPT_RHPORT);
    }

    USB_HOST_MANAGER.shutdown();
    HAL_NVIC_DisableIRQ(USB_DEV_FS_IRQn);
    HAL_NVIC_DisableIRQ(USB_HOST_HS_IRQn);
#ifdef __HAL_RCC_USB2_OTG_FS_CLK_DISABLE
    __HAL_RCC_USB2_OTG_FS_CLK_DISABLE();
#endif
#ifdef __HAL_RCC_USB_OTG_HS_CLK_DISABLE
    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
#endif
#ifdef __HAL_RCC_USB1_OTG_HS_CLK_DISABLE
    __HAL_RCC_USB1_OTG_HS_CLK_DISABLE();
#endif
}

[[noreturn]] static void enter_standby_now()
{
    configure_wkup1_for_standby();
    HAL_SuspendTick();
    HAL_PWR_EnterSTANDBYMode();
    NVIC_SystemReset();
    while (true) {
    }
}

static void request_standby()
{
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

    if (!CONNECTION_MANAGER.ensureRfSleeping(RfPowerReason::SystemSleep)) {
        LOG_ERROR("SYSTEM_SLEEP", "CH584 sleep command failed; entering Standby anyway");
    }
    Logger_Flush();

    enter_standby_now();
}

static void reset_runtime_hold()
{
    s_rotaryHoldActive = false;
    s_rotaryHoldStartMs = 0;
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
    if (!s_bootedFromStandby || !s_wakeupFromPa0) {
        (void)HAL_PWREx_ClearWakeupFlag(PWR_WAKEUP_FLAG_ALL);
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
        return;
    }

    init_pa0_input_for_hold_check();

    const uint32_t start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < SYSTEM_SLEEP_HOLD_MS) {
        if (!is_pa0_down()) {
            enter_standby_now();
        }
        HAL_Delay(5u);
    }

    s_wakeHoldConfirmed = true;
    s_waitPa0Release = true;
    reset_runtime_hold();
    (void)HAL_PWREx_ClearWakeupFlag(PWR_WAKEUP_FLAG_ALL);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
}

extern "C" void SystemSleep_HandleWakeRecovery(void)
{
    if (!s_wakeHoldConfirmed) {
        return;
    }

    LOG_INFO("SYSTEM_SLEEP", "Standby wake confirmed, restoring runtime");

#if HAS_LED == 1
    LEDS_MANAGER.setup();
#endif

    const ConnectionMode mode = STORAGE_MANAGER.getConnectionMode();
    if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
        if (!CONNECTION_MANAGER.wakeRfFromSleep(RfPowerReason::SystemWake)) {
            LOG_ERROR("SYSTEM_SLEEP", "CH584 wake failed after Standby");
        } else if (!CONNECTION_MANAGER.restoreRfRuntime(STORAGE_MANAGER.getWirelessReportRate())) {
            LOG_ERROR("SYSTEM_SLEEP", "CH584 runtime restore failed after Standby");
        }
    } else {
        (void)CONNECTION_MANAGER.ensureRfSleeping(RfPowerReason::UsbMode);
    }
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

    if ((uint32_t)(nowMs - s_rotaryHoldStartMs) >= SYSTEM_SLEEP_HOLD_MS) {
        request_standby();
    }
}

extern "C" bool SystemSleep_ShouldSuppressRotaryLongAction(void)
{
    return s_sleepEntering || s_waitPa0Release || s_rotaryHoldActive;
}
