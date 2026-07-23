#include "power_manager.hpp"

#include "board_cfg.h"
#include "adc.h"
#include "adc_btns/adc_manager.hpp"
#include "connection_manager.hpp"
#include "storagemanager.hpp"
#include "system_sleep_manager.hpp"

#include "stm32h7xx_hal.h"

static constexpr uint32_t POWER_VREF_MV = 3300u;
static constexpr uint32_t POWER_ADC_FULL_SCALE = 65535u;

static constexpr uint32_t POWER_MEAS_INTERVAL_MS = 1000u;
static constexpr uint32_t POWER_ADC_TIMEOUT_MS = 10u;
static constexpr uint32_t POWER_FULL_BATTERY_MV = 4200u;
static constexpr uint32_t POWER_LOW_BATTERY_MV = 3450u;
static constexpr uint32_t POWER_FORCE_SLEEP_MV = 3200u;
static constexpr uint32_t POWER_DISCHARGE_CUTOFF_MV = 2750u;
static constexpr uint8_t POWER_FORCE_SLEEP_CONFIRM_COUNT = 3u;

static_assert(POWER_DISCHARGE_CUTOFF_MV < POWER_FORCE_SLEEP_MV, "Force sleep must stay above cell cutoff");
static_assert(POWER_FORCE_SLEEP_MV < POWER_LOW_BATTERY_MV, "Low warning must stay above force sleep");
static_assert(POWER_LOW_BATTERY_MV < POWER_FULL_BATTERY_MV, "Low warning must stay below full charge");

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
    vbus_present = isVbusPresent();
    fast_charging = isFastChargeDetected();
    last_mode_poll_ms = HAL_GetTick();
    last_voltage_update_ms = 0;
    meas_stage = MeasureStage::Idle;
    low_sleep_confirm_count = 0;
}

void PowerManager::loop()
{
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_mode_poll_ms) >= 50u)
    {
        last_mode_poll_ms = now;
        vbus_present = isVbusPresent();
        fast_charging = isFastChargeDetected();
        if (vbus_present)
        {
            low_sleep_confirm_count = 0;
        }
    }

    if (meas_stage == MeasureStage::Idle)
    {
        if ((uint32_t)(now - last_voltage_update_ms) >= POWER_MEAS_INTERVAL_MS)
        {
            if (canUseAdcNow())
            {
                startVoltageMeasurementCycle();
            }
        }
    }
    else
    {
        processVoltageMeasurement();
    }
}

PowerChargeState PowerManager::getChargeState() const
{
    if (!vbus_present)
    {
        return PowerChargeState::Discharging;
    }
    return PowerChargeState::Charging;
}

float PowerManager::getTotalSocPercent() const
{
    const float soc = socFromMv(battery_mv);
    if (soc < 0.0f) return 0.0f;
    if (soc > 100.0f) return 100.0f;
    return soc;
}

PowerBatteryVoltages PowerManager::getVoltages() const
{
    return PowerBatteryVoltages{battery_mv, battery_mv, battery_mv};
}

PowerBatteryId PowerManager::getActiveDischargeBattery() const
{
    return PowerBatteryId::H1;
}

bool PowerManager::isVoltageValid() const
{
    return voltage_valid;
}

bool PowerManager::isFastCharging() const
{
    return fast_charging;
}

bool PowerManager::isLowBattery() const
{
    return voltage_valid && battery_mv < POWER_LOW_BATTERY_MV;
}

bool PowerManager::prepareSystemSleep()
{
    return CONNECTION_MANAGER.ensureRfSleeping(RfPowerReason::SystemSleep);
}

bool PowerManager::restoreSystemWake()
{
    if (!CONNECTION_MANAGER.wakeRfFromSleep(RfPowerReason::SystemWake))
    {
        return false;
    }

    const ConnectionMode mode = STORAGE_MANAGER.getConnectionMode();
    if (mode == ConnectionMode::CONNECTION_MODE_RF24G)
    {
        return CONNECTION_MANAGER.restoreRfRuntime(STORAGE_MANAGER.getWirelessReportRate());
    }

    return CONNECTION_MANAGER.ensureRfSleeping(RfPowerReason::UsbMode);
}

void PowerManager::configureGpios()
{
    enable_gpio_clock(VBUS_STATUS_PORT);
    enable_gpio_clock(FAST_CHARGE_STATUS_PORT);
    enable_gpio_clock(BAT_STATUS_PORT);
    enable_gpio_clock(BAT_H1_CHANNEL_CTRL_PORT);
    enable_gpio_clock(BAT_H2_CHANNEL_CTRL_PORT);
    enable_gpio_clock(VBAT_SENSE_ADC_PORT);
    enable_gpio_clock(VBAT_H1_SENSE_CTRL_PORT);
    enable_gpio_clock(VBAT_BAT_SENSE_CTRL_PORT);
    enable_gpio_clock(VBAT_H2_SENSE_CTRL_PORT);

    GPIO_InitTypeDef init = {0};

    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = VBUS_STATUS_PIN;
    HAL_GPIO_Init(VBUS_STATUS_PORT, &init);

    init.Pin = FAST_CHARGE_STATUS_PIN;
    HAL_GPIO_Init(FAST_CHARGE_STATUS_PORT, &init);

    init.Pin = BAT_STATUS_PIN;
    HAL_GPIO_Init(BAT_STATUS_PORT, &init);

    init.Mode = GPIO_MODE_OUTPUT_OD;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = BAT_H1_CHANNEL_CTRL_PIN;
    HAL_GPIO_Init(BAT_H1_CHANNEL_CTRL_PORT, &init);
    init.Pin = BAT_H2_CHANNEL_CTRL_PIN;
    HAL_GPIO_Init(BAT_H2_CHANNEL_CTRL_PORT, &init);

    HAL_GPIO_WritePin(BAT_H1_CHANNEL_CTRL_PORT, BAT_H1_CHANNEL_CTRL_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BAT_H2_CHANNEL_CTRL_PORT, BAT_H2_CHANNEL_CTRL_PIN, GPIO_PIN_SET);

    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = VBAT_H1_SENSE_CTRL_PIN;
    HAL_GPIO_Init(VBAT_H1_SENSE_CTRL_PORT, &init);
    init.Pin = VBAT_BAT_SENSE_CTRL_PIN;
    HAL_GPIO_Init(VBAT_BAT_SENSE_CTRL_PORT, &init);
    init.Pin = VBAT_H2_SENSE_CTRL_PIN;
    HAL_GPIO_Init(VBAT_H2_SENSE_CTRL_PORT, &init);

    HAL_GPIO_WritePin(VBAT_H1_SENSE_CTRL_PORT, VBAT_H1_SENSE_CTRL_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(VBAT_BAT_SENSE_CTRL_PORT, VBAT_BAT_SENSE_CTRL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(VBAT_H2_SENSE_CTRL_PORT, VBAT_H2_SENSE_CTRL_PIN, GPIO_PIN_RESET);

    init.Mode = GPIO_MODE_ANALOG;
    init.Pull = GPIO_NOPULL;
    init.Pin = VBAT_SENSE_ADC_PIN;
    HAL_GPIO_Init(VBAT_SENSE_ADC_PORT, &init);
}

bool PowerManager::isVbusPresent() const
{
    return (HAL_GPIO_ReadPin(VBUS_STATUS_PORT, VBUS_STATUS_PIN) == GPIO_PIN_SET);
}

bool PowerManager::isFastChargeDetected() const
{
    return isVbusPresent() &&
           (HAL_GPIO_ReadPin(FAST_CHARGE_STATUS_PORT, FAST_CHARGE_STATUS_PIN) == GPIO_PIN_SET);
}

bool PowerManager::canUseAdcNow() const
{
    if (ADCManager::getInstance().getADCMode() == ADC_MODE_LOW_LATENCY &&
        ADCManager::getInstance().isDmaSamplingActive())
    {
        return false;
    }
    return true;
}

void PowerManager::startVoltageMeasurementCycle()
{
    meas_stage = MeasureStage::Setup;
    meas_stage_start_ms = HAL_GetTick();
    adc_configured_for_batt = false;
}

void PowerManager::processVoltageMeasurement()
{
    const uint32_t now = HAL_GetTick();

    if (meas_stage == MeasureStage::Setup)
    {
        if (!configureAdcForBattery())
        {
            restoreAdcForButtons();
            meas_stage = MeasureStage::Idle;
            return;
        }
        adc_configured_for_batt = true;
        meas_stage = MeasureStage::StartAdc;
        meas_stage_start_ms = now;
        return;
    }

    if (meas_stage == MeasureStage::StartAdc)
    {
        if (!startSingleAdc())
        {
            restoreAdcForButtons();
            meas_stage = MeasureStage::Idle;
            return;
        }
        meas_stage = MeasureStage::WaitAdc;
        meas_stage_start_ms = now;
        return;
    }

    if (meas_stage == MeasureStage::WaitAdc)
    {
        if (!pollSingleAdcDone(POWER_ADC_TIMEOUT_MS))
        {
            if ((uint32_t)(now - meas_stage_start_ms) > POWER_ADC_TIMEOUT_MS)
            {
                restoreAdcForButtons();
                meas_stage = MeasureStage::Idle;
            }
            return;
        }

        const uint32_t mv = rawToBattMv(adc_single_value);
        battery_mv = mv;
        restoreAdcForButtons();

        last_voltage_update_ms = now;
        voltage_valid = true;
        processLowVoltageProtection();
        meas_stage = MeasureStage::Idle;
    }
}

void PowerManager::processLowVoltageProtection()
{
    if (!voltage_valid || vbus_present)
    {
        low_sleep_confirm_count = 0;
        return;
    }

    if (battery_mv <= POWER_FORCE_SLEEP_MV)
    {
        if (low_sleep_confirm_count < POWER_FORCE_SLEEP_CONFIRM_COUNT)
        {
            low_sleep_confirm_count++;
        }
        if (low_sleep_confirm_count >= POWER_FORCE_SLEEP_CONFIRM_COUNT)
        {
            SystemSleep_RequestStandby();
        }
    }
    else
    {
        low_sleep_confirm_count = 0;
    }
}

bool PowerManager::configureAdcForBattery()
{
    adc_mode_before_batt = ADCManager::getInstance().getADCMode();
    HAL_ADC_Stop_DMA(&hadc2);
    HAL_ADC_Stop(&hadc2);

    hadc2.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
    hadc2.Init.Resolution = ADC_RESOLUTION_16B;
    hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc2.Init.LowPowerAutoWait = DISABLE;
    hadc2.Init.ContinuousConvMode = DISABLE;
    hadc2.Init.NbrOfConversion = 1;
    hadc2.Init.DiscontinuousConvMode = DISABLE;
    hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc2.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc2.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc2.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    hadc2.Init.OversamplingMode = DISABLE;

    if (HAL_ADC_Init(&hadc2) != HAL_OK)
    {
        return false;
    }

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = VBAT_SENSE_ADC_CHANNEL;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = BOARD_ADC_SAMPLE_TIME;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    sConfig.OffsetSignedSaturation = DISABLE;
    if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
    {
        return false;
    }

    return true;
}

void PowerManager::restoreAdcForButtons()
{
    MX_ADC2_Init();
    if (adc_mode_before_batt == ADC_MODE_INPUT_CONTINUOUS ||
        adc_mode_before_batt == ADC_MODE_CONTINUOUS)
    {
        ADCManager::getInstance().startContinuousSampling();
    }
    adc_configured_for_batt = false;
}

bool PowerManager::startSingleAdc()
{
    if (HAL_ADC_Start(&hadc2) != HAL_OK)
    {
        return false;
    }
    return true;
}

bool PowerManager::pollSingleAdcDone(uint32_t timeout_ms)
{
    const HAL_StatusTypeDef st = HAL_ADC_PollForConversion(&hadc2, timeout_ms);
    if (st == HAL_OK)
    {
        adc_single_value = HAL_ADC_GetValue(&hadc2);
        HAL_ADC_Stop(&hadc2);
        return true;
    }
    return false;
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
    static constexpr uint32_t discharge_mv[] = {
        POWER_FORCE_SLEEP_MV, 3300u, POWER_LOW_BATTERY_MV, 3600u, 3700u,
        3800u, 3900u, 4000u, 4100u, POWER_FULL_BATTERY_MV
    };
    static constexpr float discharge_soc[] = {
        0.0f, 5.0f, 10.0f, 20.0f, 35.0f,
        50.0f, 65.0f, 80.0f, 90.0f, 100.0f
    };
    static constexpr uint32_t charge_mv[] = {
        POWER_FORCE_SLEEP_MV, 3500u, 3700u, 3800u, 3900u,
        4000u, 4100u, 4150u, POWER_FULL_BATTERY_MV
    };
    static constexpr float charge_soc[] = {
        0.0f, 10.0f, 25.0f, 40.0f, 55.0f,
        70.0f, 85.0f, 95.0f, 100.0f
    };

    const uint32_t* curve_mv = vbus_present ? charge_mv : discharge_mv;
    const float* curve_soc = vbus_present ? charge_soc : discharge_soc;
    const uint8_t curve_count = vbus_present
        ? (uint8_t)(sizeof(charge_mv) / sizeof(charge_mv[0]))
        : (uint8_t)(sizeof(discharge_mv) / sizeof(discharge_mv[0]));

    if (mv <= curve_mv[0]) return curve_soc[0];
    if (mv >= curve_mv[curve_count - 1u]) return curve_soc[curve_count - 1u];

    for (uint8_t i = 0; i < (uint8_t)(curve_count - 1u); i++)
    {
        if (mv < curve_mv[i + 1u])
        {
            const float span_mv = (float)(curve_mv[i + 1u] - curve_mv[i]);
            const float span_soc = curve_soc[i + 1u] - curve_soc[i];
            return curve_soc[i] + (float)(mv - curve_mv[i]) * (span_soc / span_mv);
        }
    }
    return curve_soc[curve_count - 1u];
}
