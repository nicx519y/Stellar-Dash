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
constexpr uint32_t kInitialAdcTimeoutMs = 1200u;
constexpr uint32_t kInitialAdcPollMs = 20u;
constexpr uint16_t kInputProfile9vThresholdMv = 7000u;
constexpr uint16_t kCharge5vInputMinimumMv = 4200u;
constexpr uint16_t kCharge5vInputMaximumMv = 6000u;
constexpr uint16_t kCharge9vInputMinimumMv = 7500u;
constexpr uint16_t kCharge9vInputMaximumMv = 10500u;
constexpr uint16_t kLowBatteryMv = PowerSnapshot::lowBatteryMv;
constexpr uint16_t kForceSleepMv = 3200u;
constexpr uint8_t kForceSleepConfirmCount = 3u;
constexpr uint8_t kGaugeAlertSocPercent = 10u;

constexpr uint32_t kIrqCharger = 1u << 0;
constexpr uint32_t kIrqGauge = 1u << 1;

static_assert(kForceSleepMv < kLowBatteryMv,
              "Low-battery warning must stay above forced sleep");

BQ25895_InputProfile inputProfileForVbus(uint16_t vbus_mv)
{
    return vbus_mv >= kInputProfile9vThresholdMv
               ? BQ25895_INPUT_PROFILE_9V_1P5A
               : BQ25895_INPUT_PROFILE_5V_1P5A;
}

const char* inputProfileName(BQ25895_InputProfile profile)
{
    return profile == BQ25895_INPUT_PROFILE_9V_1P5A ? "9V" : "5V";
}

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
    const uint32_t flags = consumeIrqFlags();
    pending_irq_flags_ |= flags;
    if ((flags & kIrqCharger) != 0u) { setChargingEnabled(false); }

    if (poll_phase_ == PollPhase::Idle) {
        const bool recovery = (!snapshot_.charger_online || !snapshot_.gauge_online || !profile_valid_) &&
            (uint32_t)(now - last_reinitialize_ms_) >= kDeviceRetryIntervalMs;
        if (!recovery && pending_irq_flags_ == 0u &&
            (uint32_t)(now - last_poll_ms_) < kPowerPollIntervalMs) { return; }
        last_poll_ms_ = poll_started_ms_ = now;
        poll_clear_gauge_ = (pending_irq_flags_ & kIrqGauge) != 0u;
        pending_irq_flags_ = 0;
        poll_recovery_ = recovery;
        profile_repair_attempted_ = false;
        poll_fault_ = 0;
        poll_charger_ = {};
        poll_gauge_ = {};
        poll_charger_ok_ = poll_gauge_ok_ = false;
        if (recovery) {
            last_reinitialize_ms_ = now;
            setChargingEnabled(false);
            profile_valid_ = false;
            PowerI2C_DeInit();
            charger_ = {};
            gauge_ = {};
            if (!PowerI2C_Init()) {
                poll_phase_ = PollPhase::Publish;
                return;
            }
            charger_.i2c = gauge_.i2c = PowerI2C_GetHandle();
            beginChargerJob(BQ25895_JOB_INIT, BQ25895_INPUT_PROFILE_5V_1P5A);
            poll_phase_ = PollPhase::InitCharger;
        } else {
            beginChargerJob(BQ25895_JOB_READ, charger_.input_profile);
            poll_phase_ = PollPhase::ReadCharger;
        }
    }
    // Discard an overdue cycle on the next service pass instead of enabling
    // charging from stale readings. Recovery includes ADC warmup.
    if (poll_phase_ != PollPhase::Publish &&
        (uint32_t)(now - poll_started_ms_) >= (poll_recovery_ ? 2000u : 500u)) {
        setChargingEnabled(false);
        PowerI2C_DeInit();
        (void)PowerI2C_Init();
        charger_.online = gauge_.online = false;
        profile_valid_ = poll_charger_ok_ = poll_gauge_ok_ = false;
        poll_phase_ = PollPhase::Publish;
    }
    stepPoll(now);
#endif
}

void PowerManager::beginChargerJob(BQ25895_JobKind kind, BQ25895_InputProfile profile)
{
    charger_job_ = {};
    charger_job_.kind = kind;
    charger_job_.profile = profile;
}

void PowerManager::beginGaugeJob(MAX17048_JobKind kind)
{
    gauge_job_ = {};
    gauge_job_.kind = kind;
    gauge_job_.alert_soc_percent = kGaugeAlertSocPercent;
}

void PowerManager::stepPoll(uint32_t now)
{
    PowerI2C_Result result;
    switch (poll_phase_) {
    case PollPhase::Idle:
        break;
    case PollPhase::InitCharger:
        result = BQ25895_Step(&charger_, &charger_job_, &poll_charger_);
        if (result == POWER_I2C_PENDING) { return; }
        if (result == POWER_I2C_DONE) {
            adc_started_ms_ = now;
            beginChargerJob(BQ25895_JOB_READ, charger_.input_profile);
            poll_phase_ = PollPhase::ReadCharger;
        } else {
            beginGaugeJob(MAX17048_JOB_INIT);
            poll_phase_ = PollPhase::InitGauge;
        }
        break;
    case PollPhase::WaitAdc:
        if ((uint32_t)(now - adc_poll_ms_) >= kInitialAdcPollMs) {
            beginChargerJob(BQ25895_JOB_READ, charger_.input_profile);
            poll_phase_ = PollPhase::ReadCharger;
        }
        break;
    case PollPhase::ReadCharger:
        result = BQ25895_Step(&charger_, &charger_job_, &poll_charger_);
        poll_fault_ |= poll_charger_.fault;
        poll_charger_.fault = poll_fault_;
        if (poll_fault_ != 0 || result == POWER_I2C_FAILED) { setChargingEnabled(false); }
        if (result == POWER_I2C_PENDING) { return; }
        poll_charger_ok_ = result == POWER_I2C_DONE;
        if (poll_recovery_ && poll_charger_ok_ &&
            !(poll_charger_.vbus_good && poll_charger_.vbus_mv >= 3900u) &&
            (uint32_t)(now - adc_started_ms_) < kInitialAdcTimeoutMs) {
            adc_poll_ms_ = now;
            poll_phase_ = PollPhase::WaitAdc;
        } else {
            beginGaugeJob(poll_recovery_ ? MAX17048_JOB_INIT : MAX17048_JOB_READ);
            poll_phase_ = poll_recovery_ ? PollPhase::InitGauge : PollPhase::ReadGauge;
        }
        break;
    case PollPhase::InitGauge:
        result = MAX17048_Step(&gauge_, &gauge_job_, &poll_gauge_);
        if (result == POWER_I2C_PENDING) { return; }
        if (result == POWER_I2C_DONE) {
            beginGaugeJob(MAX17048_JOB_READ);
            poll_phase_ = PollPhase::ReadGauge;
        } else { poll_phase_ = PollPhase::Profile; }
        break;
    case PollPhase::ReadGauge:
        result = MAX17048_Step(&gauge_, &gauge_job_, &poll_gauge_);
        if (result == POWER_I2C_FAILED) { setChargingEnabled(false); }
        if (result == POWER_I2C_PENDING) { return; }
        poll_gauge_ok_ = result == POWER_I2C_DONE;
        if (poll_gauge_ok_ && !poll_gauge_.valid) { setChargingEnabled(false); }
        if (poll_gauge_ok_ && (poll_clear_gauge_ || poll_gauge_.alert)) {
            beginGaugeJob(MAX17048_JOB_CLEAR_ALERT);
            poll_phase_ = PollPhase::ClearGauge;
        } else { poll_phase_ = PollPhase::Profile; }
        break;
    case PollPhase::ClearGauge:
        result = MAX17048_Step(&gauge_, &gauge_job_, &poll_gauge_);
        if (result == POWER_I2C_PENDING) { return; }
        poll_gauge_ok_ = result == POWER_I2C_DONE;
        if (!poll_gauge_ok_) { setChargingEnabled(false); }
        poll_phase_ = PollPhase::Profile;
        break;
    case PollPhase::Profile: {
        if (!poll_charger_ok_) { poll_phase_ = PollPhase::Publish; break; }
        const BQ25895_InputProfile desired = poll_charger_.vbus_good
            ? inputProfileForVbus(poll_charger_.vbus_mv) : charger_.input_profile;
        if (!profile_valid_ || !charger_.profile_configured || desired != charger_.input_profile) {
            setChargingEnabled(false);
            profile_valid_ = false;
            profile_repair_attempted_ = true;
            beginChargerJob(BQ25895_JOB_CONFIGURE, desired);
            poll_phase_ = PollPhase::ConfigureProfile;
        } else if ((uint32_t)(now - last_profile_check_ms_) >= kProfileVerifyIntervalMs) {
            beginChargerJob(BQ25895_JOB_VERIFY, desired);
            poll_phase_ = PollPhase::VerifyProfile;
        } else { poll_phase_ = PollPhase::Publish; }
        break;
    }
    case PollPhase::ConfigureProfile:
        result = BQ25895_Step(&charger_, &charger_job_, &poll_charger_);
        if (result == POWER_I2C_PENDING) { return; }
        if (result == POWER_I2C_DONE) {
            beginChargerJob(BQ25895_JOB_VERIFY, charger_.input_profile);
            poll_phase_ = PollPhase::VerifyProfile;
        } else {
            poll_charger_ok_ = false;
            poll_phase_ = PollPhase::Publish;
        }
        break;
    case PollPhase::VerifyProfile:
        result = BQ25895_Step(&charger_, &charger_job_, &poll_charger_);
        if (result == POWER_I2C_PENDING) { return; }
        last_profile_check_ms_ = now;
        profile_valid_ = result == POWER_I2C_DONE;
        if (!profile_valid_) {
            setChargingEnabled(false);
            if (!profile_repair_attempted_) {
                profile_repair_attempted_ = true;
                beginChargerJob(BQ25895_JOB_CONFIGURE, charger_.input_profile);
                poll_phase_ = PollPhase::ConfigureProfile;
                break;
            }
        }
        poll_phase_ = PollPhase::Publish;
        break;
    case PollPhase::Publish:
        publishSnapshot(poll_charger_, poll_gauge_, poll_charger_ok_, poll_gauge_ok_);
        poll_phase_ = PollPhase::Idle;
        break;
    }
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

bool PowerManager::isPolling() const
{
    return poll_phase_ != PollPhase::Idle || PowerI2C_AsyncBusy();
}

bool PowerManager::prepareSystemSleep()
{
    if (isPolling()) { return false; }
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
    bool charger_ready =
        BQ25895_Init(&charger_, i2c) &&
        BQ25895_EnableContinuousAdc(&charger_);
    BQ25895_State detected_input = {};
    bool adc_valid = false;
    if (charger_ready) {
        const uint32_t adc_start_ms = HAL_GetTick();
        do {
            if (!BQ25895_ReadState(&charger_, &detected_input)) {
                charger_ready = false;
                break;
            }
            adc_valid = detected_input.vbus_good && detected_input.vbus_mv >= 3900u;
            if (adc_valid) {
                break;
            }
            HAL_Delay(kInitialAdcPollMs);
        } while ((uint32_t)(HAL_GetTick() - adc_start_ms) < kInitialAdcTimeoutMs);
    }

    const BQ25895_InputProfile input_profile =
        inputProfileForVbus(adc_valid ? detected_input.vbus_mv : 0u);
    if (charger_ready) {
        charger_ready =
            BQ25895_ConfigureSafeProfile(&charger_, input_profile) &&
            BQ25895_VerifySafeProfile(&charger_);
    }
    APP_STAGE("P04A", "BQ25895 input profile: adc=%u vbus=%umV profile=%s IINLIM=1500mA VINDPM=%umV ready=%u",
              adc_valid ? 1u : 0u,
              detected_input.vbus_mv,
              inputProfileName(input_profile),
              input_profile == BQ25895_INPUT_PROFILE_9V_1P5A ? 8200u : 4400u,
              charger_ready ? 1u : 0u);
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

    if (charger_ok && charger_state.vbus_good) {
        const BQ25895_InputProfile desired_profile =
            inputProfileForVbus(charger_state.vbus_mv);
        if (!charger_.profile_configured ||
            charger_.input_profile != desired_profile) {
            setChargingEnabled(false);
            profile_valid_ =
                BQ25895_ConfigureSafeProfile(&charger_, desired_profile) &&
                BQ25895_VerifySafeProfile(&charger_);
            charger_ok = charger_.online && profile_valid_;
            APP_STAGE("P07", "BQ25895 input changed: vbus=%umV profile=%s applied=%u",
                      charger_state.vbus_mv,
                      inputProfileName(desired_profile),
                      profile_valid_ ? 1u : 0u);
        }
    }

    const uint32_t now = HAL_GetTick();
    if (charger_ok &&
        (uint32_t)(now - last_profile_check_ms_) >= kProfileVerifyIntervalMs) {
        last_profile_check_ms_ = now;
        profile_valid_ = BQ25895_VerifySafeProfile(&charger_);
        if (!profile_valid_) {
            setChargingEnabled(false);
            profile_valid_ =
                BQ25895_ConfigureSafeProfile(
                    &charger_, charger_.input_profile) &&
                BQ25895_VerifySafeProfile(&charger_);
        }
    }

    publishSnapshot(charger_state, gauge_state, charger_ok, gauge_ok);
}

void PowerManager::publishSnapshot(const BQ25895_State& charger_state,
                                   const MAX17048_State& gauge_state,
                                   bool charger_ok, bool gauge_ok)
{
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
        snapshot_.input_current_limit_ma = charger_state.input_current_limit_ma;
        snapshot_.input_current_regulation =
            charger_state.input_current_regulation;
        snapshot_.input_voltage_regulation =
            charger_state.input_voltage_regulation;
        snapshot_.vbus_present =
            charger_state.power_good && charger_state.vbus_good;
    } else {
        snapshot_.vbus_mv = 0u;
        snapshot_.charge_current_ma = 0u;
        snapshot_.input_current_limit_ma = 0u;
        snapshot_.input_current_regulation = false;
        snapshot_.input_voltage_regulation = false;
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

    const bool safe_5v_input =
        snapshot_.vbus_present &&
        snapshot_.vbus_mv >= kCharge5vInputMinimumMv &&
        snapshot_.vbus_mv <= kCharge5vInputMaximumMv;
    const bool safe_9v_input =
        snapshot_.vbus_present &&
        snapshot_.vbus_mv >= kCharge9vInputMinimumMv &&
        snapshot_.vbus_mv <= kCharge9vInputMaximumMv;
    const bool safe_input = safe_5v_input || safe_9v_input;
    if (snapshot_.vbus_present && !safe_input) {
        fault_bits |= POWER_FAULT_VBUS_OUT_OF_RANGE;
    }
    snapshot_.fault_bits = fault_bits;

    const bool fatal_charger_fault =
        charger_ok && BQ25895_IsFatalFault(charger_state.fault);
    const bool safe_to_charge =
        charger_ok && gauge_ok && gauge_state.valid && profile_valid_ &&
        safe_input && !fatal_charger_fault &&
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
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    // An IRQ arriving during a cycle requires a fresh snapshot before CE can
    // be re-enabled. Keep this check atomic with the physical GPIO write.
    enabled = enabled && irq_flags_ == 0u && pending_irq_flags_ == 0u;
    BOARD_POWER.setChargeEnabled(enabled);
    charging_enabled_ = enabled;
    if (primask == 0u) { __enable_irq(); }
}

bool PowerManager::isFastChargeDetected() const
{
    /*
     * CH224A PG is open-drain and active-low.  It remains auxiliary telemetry;
     * BQ25895 PG/VBUS ADC are authoritative for charge enable.
     */
    return HAL_GPIO_ReadPin(IS_FAST_CHARGE_PORT, IS_FAST_CHARGE_PIN) ==
           GPIO_PIN_RESET;
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
