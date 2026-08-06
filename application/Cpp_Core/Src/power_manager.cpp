#include "power_manager.hpp"

#include "board_cfg.h"
#include "board_mode.hpp"
#include "board_power.hpp"
#include "ch585_role_bootstrap.hpp"
#include "connection_manager.hpp"
#include "power_i2c_bus.h"
#include "storagemanager.hpp"
#include "system_sleep_manager.hpp"

#include "stm32h7xx_hal.h"

#ifndef POWER_DEVICE_PROBE_ENABLED
#define POWER_DEVICE_PROBE_ENABLED 1
#endif

/*
 * Fallbacks keep this driver buildable while board_cfg.h is being migrated in
 * parallel.  The names and values match the latest PCB net labels.
 */
#ifndef CHARGE_EN_N_PORT
#define CHARGE_EN_N_PORT GPIOI
#define CHARGE_EN_N_PIN GPIO_PIN_0
#endif
#ifndef IS_FAST_CHARGE_PORT
#define IS_FAST_CHARGE_PORT GPIOI
#define IS_FAST_CHARGE_PIN GPIO_PIN_2
#endif
#ifndef CHARGE_STAT_PORT
#define CHARGE_STAT_PORT GPIOI
#define CHARGE_STAT_PIN GPIO_PIN_3
#endif
#ifndef CHARGE_INT_PORT
#define CHARGE_INT_PORT GPIOI
#define CHARGE_INT_PIN GPIO_PIN_8
#endif
#ifndef MAX17048_ALERT_PORT
#define MAX17048_ALERT_PORT GPIOC
#define MAX17048_ALERT_PIN GPIO_PIN_13
#endif

namespace {

constexpr uint32_t kPowerPollIntervalMs = 1000u;
constexpr uint32_t kProfileVerifyIntervalMs = 10000u;
constexpr uint32_t kDeviceRetryIntervalMs = 5000u;
constexpr uint16_t kChargeInputMinimumMv = 8000u;
constexpr uint16_t kChargeInputMaximumMv = 10000u;
constexpr uint16_t kLowBatteryMv = 3450u;
constexpr uint16_t kForceSleepMv = 3200u;
constexpr uint8_t kForceSleepConfirmCount = 3u;
constexpr uint8_t kGaugeAlertSocPercent = 10u;

constexpr uint32_t kIrqCharger = 1u << 0;
constexpr uint32_t kIrqGauge = 1u << 1;

static_assert(kForceSleepMv < kLowBatteryMv,
              "Low-battery warning must stay above forced sleep");

void enableGpioClock(GPIO_TypeDef* port)
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

}  // namespace

void PowerManager::setup()
{
    APP_STAGE("P01", "power manager setup begin");
    configureSafetyGpios();
    setChargingEnabled(false);
    APP_STAGE("P02", "power safety GPIOs configured; charging disabled");

    last_poll_ms_ = HAL_GetTick();
    last_profile_check_ms_ = last_poll_ms_;
    last_reinitialize_ms_ = last_poll_ms_;
    low_sleep_confirm_count_ = 0;

    APP_STAGE("P03", "power I2C initialization begin");
    if (!PowerI2C_Init()) {
        snapshot_.fault_bits =
            POWER_FAULT_CHARGER_OFFLINE | POWER_FAULT_GAUGE_OFFLINE;
        APP_STAGE_ERROR("P03", "power I2C initialization failed; continuing without telemetry");
        APP_ERR("Power: I2C1 initialization failed; charging remains disabled");
        return;
    }
    APP_STAGE("P04", "power I2C initialized; device probes begin");

#if POWER_DEVICE_PROBE_ENABLED
    const bool devices_ready = initializeDevices();
    APP_STAGE("P05", "power device probes complete: ready=%u charger=%u gauge=%u",
              devices_ready ? 1u : 0u,
              charger_.online ? 1u : 0u,
              gauge_.online ? 1u : 0u);
    refreshSnapshot(false);
    APP_STAGE("P06", "power snapshot complete: valid=%u cell=%umV faults=0x%04X",
              snapshot_.valid ? 1u : 0u,
              snapshot_.cell_mv,
              snapshot_.fault_bits);
#else
    snapshot_.fault_bits = POWER_FAULT_CHARGER_OFFLINE |
                           POWER_FAULT_GAUGE_OFFLINE |
                           POWER_FAULT_PROFILE_INVALID;
    APP_STAGE("P05", "power device probes disabled for board bring-up; telemetry offline");
#endif

    APP_DBG(
        "Power: BQ25895=%u MAX17048=%u profile=%u cell=%umV soc=%u.%u%%",
        snapshot_.charger_online ? 1u : 0u,
        snapshot_.gauge_online ? 1u : 0u,
        profile_valid_ ? 1u : 0u,
        snapshot_.cell_mv,
        snapshot_.soc_permille / 10u,
        snapshot_.soc_permille % 10u);
}

void PowerManager::loop()
{
#if !POWER_DEVICE_PROBE_ENABLED
    /* Charging stays disabled and the offline snapshot remains authoritative
     * until a production build enables the qualified I2C device probes. */
    (void)consumeIrqFlags();
    return;
#else
    const uint32_t now = HAL_GetTick();
    const uint32_t irq_flags = consumeIrqFlags();
    const bool poll_due = (uint32_t)(now - last_poll_ms_) >= kPowerPollIntervalMs;

    if (irq_flags != 0u || poll_due) {
        last_poll_ms_ = now;
        refreshSnapshot((irq_flags & kIrqGauge) != 0u);
    }

    if ((!snapshot_.charger_online || !snapshot_.gauge_online || !profile_valid_) &&
        (uint32_t)(now - last_reinitialize_ms_) >= kDeviceRetryIntervalMs) {
        last_reinitialize_ms_ = now;
        setChargingEnabled(false);
        PowerI2C_DeInit();
        if (PowerI2C_Init()) {
            (void)initializeDevices();
            refreshSnapshot(false);
        }
    }
#endif
}

PowerSnapshot PowerManager::getSnapshot() const
{
    return snapshot_;
}

PowerChargeState PowerManager::getChargeState() const
{
    return snapshot_.charge_state;
}

float PowerManager::getTotalSocPercent() const
{
    return static_cast<float>(snapshot_.soc_permille) / 10.0f;
}

PowerBatteryVoltages PowerManager::getVoltages() const
{
    /*
     * Frozen RF compatibility: H1 carries the single-pack voltage and H2 is
     * deliberately zero so no new/false second-battery state can be emitted.
     */
    return PowerBatteryVoltages{snapshot_.cell_mv, 0u, snapshot_.cell_mv};
}

PowerBatteryId PowerManager::getActiveDischargeBattery() const
{
    return PowerBatteryId::H1;
}

bool PowerManager::isVoltageValid() const
{
    return snapshot_.valid;
}

bool PowerManager::isFastCharging() const
{
    return snapshot_.fast_charge &&
           snapshot_.charge_state == PowerChargeState::Charging;
}

bool PowerManager::isLowBattery() const
{
    return snapshot_.valid && snapshot_.cell_mv < kLowBatteryMv;
}

bool PowerManager::prepareSystemSleep()
{
    if (BOARD_MODE.isStable() &&
        BOARD_MODE.current() == BoardMode::Rf &&
        CH585_ROLE_BOOTSTRAP.isLocked() &&
        CH585_ROLE_BOOTSTRAP.role() == Ch585Role::Rf) {
        return CONNECTION_MANAGER.ensureRfSleeping(
            RfPowerReason::SystemSleep);
    }
    return true;
}

bool PowerManager::restoreSystemWake()
{
    if (!BOARD_MODE.isStable()) {
        return false;
    }
    if (BOARD_MODE.current() == BoardMode::Usb &&
        CH585_ROLE_BOOTSTRAP.isLocked() &&
        (CH585_ROLE_BOOTSTRAP.role() == Ch585Role::Usb ||
         CH585_ROLE_BOOTSTRAP.role() == Ch585Role::Maintenance)) {
        return true;
    }
    if (BOARD_MODE.current() == BoardMode::Rf &&
        CH585_ROLE_BOOTSTRAP.isLocked() &&
        CH585_ROLE_BOOTSTRAP.role() == Ch585Role::Rf &&
        CONNECTION_MANAGER.wakeRfFromSleep(
            RfPowerReason::SystemWake)) {
        return CONNECTION_MANAGER.restoreRfRuntime(
            STORAGE_MANAGER.getWirelessReportRate());
    }
    return false;
}

void PowerManager::notifyChargerIrqFromISR()
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    irq_flags_ |= kIrqCharger;
    if (primask == 0u) {
        __enable_irq();
    }
}

void PowerManager::notifyGaugeAlertFromISR()
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    irq_flags_ |= kIrqGauge;
    if (primask == 0u) {
        __enable_irq();
    }
}

void PowerManager::configureSafetyGpios()
{
    if (!BOARD_POWER.isInitialized()) {
        BOARD_POWER.setup();
    }

    enableGpioClock(IS_FAST_CHARGE_PORT);
    enableGpioClock(CHARGE_STAT_PORT);
    enableGpioClock(CHARGE_INT_PORT);
    enableGpioClock(MAX17048_ALERT_PORT);
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = IS_FAST_CHARGE_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(IS_FAST_CHARGE_PORT, &gpio);

    gpio.Pin = CHARGE_STAT_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(CHARGE_STAT_PORT, &gpio);

    gpio.Pin = CHARGE_INT_PIN;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(CHARGE_INT_PORT, &gpio);

    gpio.Pin = MAX17048_ALERT_PIN;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MAX17048_ALERT_PORT, &gpio);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6u, 0u);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    /* EXTI15_10 is shared with latency-sensitive CH585 W_INT on PE10. */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, CH585_IRQ_EXTI_IRQn_PRIO, 0u);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

bool PowerManager::initializeDevices()
{
    setChargingEnabled(false);
    profile_valid_ = false;

    I2C_HandleTypeDef* const i2c = PowerI2C_GetHandle();
    const bool charger_ready =
        BQ25895_Init(&charger_, i2c) &&
        BQ25895_ConfigureSafeProfile(&charger_) &&
        BQ25895_EnableContinuousAdc(&charger_) &&
        BQ25895_VerifySafeProfile(&charger_);
    profile_valid_ = charger_ready;

    const bool gauge_ready =
        MAX17048_Init(&gauge_, i2c) &&
        MAX17048_ConfigureAlert(&gauge_, kGaugeAlertSocPercent);

    if (!charger_ready) {
        charger_.online = false;
    }
    if (!gauge_ready) {
        gauge_.online = false;
    }
    last_profile_check_ms_ = HAL_GetTick();
    return charger_ready && gauge_ready;
}

void PowerManager::refreshSnapshot(bool clearGaugeAlert)
{
    BQ25895_State charger_state = {};
    MAX17048_State gauge_state = {};

    bool charger_ok = charger_.online &&
                      BQ25895_ReadState(&charger_, &charger_state);
    bool gauge_ok = gauge_.online &&
                    MAX17048_ReadState(&gauge_, &gauge_state);

    if (gauge_ok && (clearGaugeAlert || gauge_state.alert)) {
        gauge_ok = MAX17048_ClearAlert(&gauge_);
    }

    const uint32_t now = HAL_GetTick();
    if (charger_ok &&
        (uint32_t)(now - last_profile_check_ms_) >= kProfileVerifyIntervalMs) {
        last_profile_check_ms_ = now;
        profile_valid_ = BQ25895_VerifySafeProfile(&charger_);
        if (!profile_valid_) {
            setChargingEnabled(false);
        }
    }

    snapshot_.charger_online = charger_ok;
    snapshot_.gauge_online = gauge_ok;
    snapshot_.valid = gauge_ok && gauge_state.valid;

    if (gauge_ok) {
        snapshot_.cell_mv = gauge_state.cell_mv;
        snapshot_.soc_permille = gauge_state.soc_permille;
    } else {
        snapshot_.cell_mv = 0u;
        snapshot_.soc_permille = 0u;
    }

    if (charger_ok) {
        snapshot_.vbus_mv = charger_state.vbus_mv;
        snapshot_.charge_current_ma = charger_state.charge_current_ma;
        snapshot_.vbus_present =
            charger_state.power_good && charger_state.vbus_good;
    } else {
        snapshot_.vbus_mv = 0u;
        snapshot_.charge_current_ma = 0u;
        snapshot_.vbus_present = false;
        profile_valid_ = false;
    }
    snapshot_.fast_charge =
        snapshot_.vbus_present && isFastChargeDetected();

    uint16_t fault_bits = charger_ok ? charger_state.fault : 0u;
    if (!charger_ok) {
        fault_bits |= POWER_FAULT_CHARGER_OFFLINE;
    }
    if (!gauge_ok) {
        fault_bits |= POWER_FAULT_GAUGE_OFFLINE;
    }
    if (!profile_valid_) {
        fault_bits |= POWER_FAULT_PROFILE_INVALID;
    }

    const bool safe_9v_input =
        snapshot_.vbus_present &&
        snapshot_.vbus_mv >= kChargeInputMinimumMv &&
        snapshot_.vbus_mv <= kChargeInputMaximumMv;
    if (snapshot_.vbus_present && !safe_9v_input) {
        fault_bits |= POWER_FAULT_VBUS_OUT_OF_RANGE;
    }
    snapshot_.fault_bits = fault_bits;

    const bool fatal_charger_fault =
        charger_ok && BQ25895_IsFatalFault(charger_state.fault);
    const bool safe_to_charge =
        charger_ok && gauge_ok && gauge_state.valid && profile_valid_ &&
        safe_9v_input && !fatal_charger_fault &&
        !BOARD_POWER.isSafeLatched();
    setChargingEnabled(safe_to_charge);

    if (!charger_ok) {
        snapshot_.charge_state = PowerChargeState::Unknown;
    } else if (fatal_charger_fault) {
        snapshot_.charge_state = PowerChargeState::Fault;
    } else if (charger_state.charge_status == 3u) {
        snapshot_.charge_state = PowerChargeState::Full;
    } else if (charging_enabled_ &&
               (charger_state.charge_status == 1u ||
                charger_state.charge_status == 2u)) {
        snapshot_.charge_state = PowerChargeState::Charging;
    } else {
        snapshot_.charge_state = PowerChargeState::Discharging;
    }

    processLowVoltageProtection();
}

void PowerManager::processLowVoltageProtection()
{
    if (!snapshot_.valid || snapshot_.vbus_present) {
        low_sleep_confirm_count_ = 0;
        return;
    }

    if (snapshot_.cell_mv <= kForceSleepMv) {
        if (low_sleep_confirm_count_ < kForceSleepConfirmCount) {
            ++low_sleep_confirm_count_;
        }
        if (low_sleep_confirm_count_ >= kForceSleepConfirmCount) {
            SystemSleep_RequestStandby();
        }
    } else {
        low_sleep_confirm_count_ = 0;
    }
}

void PowerManager::setChargingEnabled(bool enabled)
{
    BOARD_POWER.setChargeEnabled(enabled);
    charging_enabled_ = enabled;
}

bool PowerManager::isFastChargeDetected() const
{
    /*
     * CH224 PG polarity still requires board validation.  It is exposed only
     * as auxiliary telemetry; BQ25895 PG/VBUS ADC remain authoritative for CE.
     */
    return HAL_GPIO_ReadPin(IS_FAST_CHARGE_PORT, IS_FAST_CHARGE_PIN) ==
           GPIO_PIN_SET;
}

uint32_t PowerManager::consumeIrqFlags()
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t flags = irq_flags_;
    irq_flags_ = 0u;
    if (primask == 0u) {
        __enable_irq();
    }
    return flags;
}

extern "C" void PowerManager_NotifyChargerIrqFromISR(void)
{
    POWER_MANAGER.notifyChargerIrqFromISR();
}

extern "C" void PowerManager_NotifyGaugeAlertFromISR(void)
{
    POWER_MANAGER.notifyGaugeAlertFromISR();
}
