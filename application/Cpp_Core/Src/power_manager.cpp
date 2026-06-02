#include "power_manager.hpp"

#include "board_cfg.h"
#include "adc_btns/adc_manager.hpp"
#include "battery_soc_tables.hpp"
#include "connection_manager.hpp"
#include "system_logger.h"

#include "stm32h7xx_hal.h"

extern "C" void SystemClock_Config(void);

static constexpr uint32_t POWER_VREF_MV = 3300u;
static constexpr uint32_t POWER_ADC_FULL_SCALE = 65535u;
static constexpr uint32_t POWER_STATUS_POLL_INTERVAL_MS = 50u;
static constexpr uint32_t POWER_STATUS_LOG_INTERVAL_MS = 5000u;
static constexpr uint32_t BATTERY_EMPTY_MV = 3150u;
static constexpr uint32_t RUN_LOW_SLEEP_MV = 3150u;
static constexpr uint32_t RUN_PA0_SLEEP_HOLD_MS = 8000u;

static battery_soc::Profile select_soc_profile(bool charging, bool fast_charging)
{
    if (fast_charging)
    {
        return battery_soc::Profile::FastCharge;
    }
    if (charging)
    {
        return battery_soc::Profile::SlowCharge;
    }
    return battery_soc::Profile::Discharge;
}

static void enable_gpio_clock(GPIO_TypeDef* port)
{
    if (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
    else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
    else if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
    else if (port == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
    else if (port == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); }
    else if (port == GPIOI) { __HAL_RCC_GPIOI_CLK_ENABLE(); }
    else if (port == GPIOJ) { __HAL_RCC_GPIOJ_CLK_ENABLE(); }
    else if (port == GPIOK) { __HAL_RCC_GPIOK_CLK_ENABLE(); }
}

void PowerManager::setup()
{
    configureGpios();
    vbus_present = readVbusPresent();
    fast_charging_pin_high = readFastChargingPin();
    last_mode_poll_ms = HAL_GetTick();
    last_status_log_ms = last_mode_poll_ms;
}

void PowerManager::loop()
{
    if (standby_sleep_in_progress)
    {
        return;
    }

    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_mode_poll_ms) < POWER_STATUS_POLL_INTERVAL_MS)
    {
        return;
    }

    last_mode_poll_ms = now;
    vbus_present = readVbusPresent();
    fast_charging_pin_high = readFastChargingPin();
    const bool power_button_pressed = readPowerButtonPressed();
    if (power_button_pressed)
    {
        if (power_button_pressed_since_ms == 0u)
        {
            power_button_pressed_since_ms = now;
        }
        else if ((uint32_t)(now - power_button_pressed_since_ms) >= RUN_PA0_SLEEP_HOLD_MS)
        {
            requestStandbySleep("PA0 long press");
            return;
        }
    }
    else
    {
        power_button_pressed_since_ms = 0u;
    }

    const uint32_t batt_mv = rawToBattMv(ADCManager::getInstance().readBatteryRawValue());
    if (!isCharging() && batt_mv != 0u && batt_mv <= RUN_LOW_SLEEP_MV)
    {
        requestStandbySleep("low battery");
        return;
    }

    if ((uint32_t)(now - last_status_log_ms) >= POWER_STATUS_LOG_INTERVAL_MS)
    {
        last_status_log_ms = now;
        const battery_soc::Profile soc_profile = select_soc_profile(isCharging(), isFastCharging());
        APP_DBG("[POWER][5s] PG8(VBUS)=%u PG9(FAST)=%u BAT=%lumV SOC=%.1f%% SOC_MODE=%s",
                vbus_present ? 1u : 0u,
                fast_charging_pin_high ? 1u : 0u,
                (unsigned long)batt_mv,
                battery_soc::lookupSocPercent(soc_profile, batt_mv),
                battery_soc::profileName(soc_profile));
    }
}

bool PowerManager::isCharging() const
{
    return vbus_present;
}

bool PowerManager::isFastCharging() const
{
    return isCharging() && fast_charging_pin_high;
}

float PowerManager::getBatterySocPercent() const
{
    return socFromMv(getBatteryVoltageMv());
}

uint32_t PowerManager::getBatteryVoltageMv() const
{
    return rawToBattMv(ADCManager::getInstance().readBatteryRawValue());
}

void PowerManager::configureWakeup()
{
    enable_gpio_clock(POWER_WAKEUP_PORT);

    GPIO_InitTypeDef init = {0};
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = POWER_WAKEUP_PIN;
    HAL_GPIO_Init(POWER_WAKEUP_PORT, &init);

    HAL_PWREx_DisableWakeUpPin(POWER_WAKEUP_PWR_PIN);
    HAL_PWREx_ClearWakeupFlag(POWER_WAKEUP_FLAG);

    PWREx_WakeupPinTypeDef wakeup = {0};
    wakeup.WakeUpPin = POWER_WAKEUP_PWR_PIN;
    wakeup.PinPolarity = PWR_PIN_POLARITY_LOW;
    wakeup.PinPull = PWR_PIN_PULL_UP;
    HAL_PWREx_EnableWakeUpPin(&wakeup);
}

void PowerManager::enterStopSleep()
{
    configureWakeup();

    HAL_SuspendTick();
    HAL_PWREx_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI, PWR_D1_DOMAIN);

    SystemClock_Config();
    HAL_ResumeTick();
    configureGpios();
    vbus_present = readVbusPresent();
    fast_charging_pin_high = readFastChargingPin();
    last_mode_poll_ms = HAL_GetTick();
    last_status_log_ms = last_mode_poll_ms;
}

void PowerManager::enterStandbySleep()
{
    configureWakeup();

    HAL_SuspendTick();
    HAL_PWREx_EnterSTANDBYMode(PWR_D1_DOMAIN);

    while (1)
    {
    }
}

void PowerManager::configureGpios()
{
    enable_gpio_clock(VBUS_STATUS_PORT);
    enable_gpio_clock(FAST_CHARGING_STATUS_PORT);
    enable_gpio_clock(POWER_WAKEUP_PORT);

    GPIO_InitTypeDef init = {0};
    init.Mode = GPIO_MODE_INPUT;
    init.Speed = GPIO_SPEED_FREQ_LOW;

    init.Pull = GPIO_NOPULL;
    init.Pin = VBUS_STATUS_PIN;
    HAL_GPIO_Init(VBUS_STATUS_PORT, &init);

    init.Pull = GPIO_PULLDOWN;
    init.Pin = FAST_CHARGING_STATUS_PIN;
    HAL_GPIO_Init(FAST_CHARGING_STATUS_PORT, &init);

    init.Pull = GPIO_PULLUP;
    init.Pin = POWER_WAKEUP_PIN;
    HAL_GPIO_Init(POWER_WAKEUP_PORT, &init);
}

bool PowerManager::readVbusPresent() const
{
    return (HAL_GPIO_ReadPin(VBUS_STATUS_PORT, VBUS_STATUS_PIN) == GPIO_PIN_SET);
}

bool PowerManager::readFastChargingPin() const
{
    return (HAL_GPIO_ReadPin(FAST_CHARGING_STATUS_PORT, FAST_CHARGING_STATUS_PIN) == GPIO_PIN_SET);
}

bool PowerManager::readPowerButtonPressed() const
{
    return (HAL_GPIO_ReadPin(POWER_WAKEUP_PORT, POWER_WAKEUP_PIN) == GPIO_PIN_RESET);
}

uint32_t PowerManager::rawToBattMv(uint32_t raw) const
{
    if (raw == 0)
        return 0;

    const uint32_t vadc_mv = (raw * POWER_VREF_MV) / POWER_ADC_FULL_SCALE;
    const uint32_t vbat_mv = (vadc_mv * 320u) / 220u;
    return vbat_mv;
}

float PowerManager::socFromMv(uint32_t mv) const
{
    if (mv <= BATTERY_EMPTY_MV)
    {
        return 0.0f;
    }
    return battery_soc::lookupSocPercent(select_soc_profile(isCharging(), isFastCharging()), mv);
}

void PowerManager::requestStandbySleep(const char* reason)
{
    standby_sleep_in_progress = true;
    APP_DBG("[POWER] enter standby reason:%s", reason != nullptr ? reason : "unknown");
    (void)CONNECTION_MANAGER.prepareRfForStandby();
    enterStandbySleep();
}
