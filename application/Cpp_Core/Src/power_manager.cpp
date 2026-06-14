#include "power_manager.hpp"

#include "board_cfg.h"
#include "adc.h"
#include "adc_btns/adc_manager.hpp"

#include "stm32h7xx_hal.h"

bool get_usb_mounted(void);

static constexpr uint32_t POWER_VREF_MV = 3300u;
static constexpr uint32_t POWER_ADC_FULL_SCALE = 65535u;

static constexpr uint32_t POWER_MEAS_INTERVAL_MS = 1000u;
static constexpr uint32_t POWER_MEAS_SETTLE_MS = 40u;
static constexpr uint32_t POWER_ADC_TIMEOUT_MS = 10u;

static constexpr uint32_t POWER_SWITCH_DIFF_MV = 120u;
static constexpr uint8_t POWER_SWITCH_CONFIRM_COUNT = 3u;
static constexpr uint32_t POWER_SWITCH_DEADTIME_MS = 5u;
static constexpr uint32_t POWER_MIN_DWELL_MS = 10000u;

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
    last_mode_poll_ms = HAL_GetTick();
    last_voltage_update_ms = 0;
    meas_stage = MeasureStage::Idle;
    switch_in_progress = false;
    switch_confirm_count = 0;
    last_switch_complete_ms = HAL_GetTick();
    switch_phase = 0;

    setChannelStates(true, false);
    active_discharge = PowerBatteryId::H1;
}

void PowerManager::loop()
{
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_mode_poll_ms) >= 50u)
    {
        last_mode_poll_ms = now;
        vbus_present = isVbusPresent();
    }

    processSwitch();

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
    const float soc = (h1_soc + h2_soc) * 0.5f;
    if (soc < 0.0f) return 0.0f;
    if (soc > 100.0f) return 100.0f;
    return soc;
}

PowerBatteryVoltages PowerManager::getVoltages() const
{
    return PowerBatteryVoltages{h1_mv, h2_mv, bat_mv};
}

PowerBatteryId PowerManager::getActiveDischargeBattery() const
{
    return active_discharge;
}

void PowerManager::configureGpios()
{
    enable_gpio_clock(VBUS_STATUS_PORT);
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

    init.Pin = BAT_STATUS_PIN;
    HAL_GPIO_Init(BAT_STATUS_PORT, &init);

    init.Mode = GPIO_MODE_OUTPUT_OD;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = BAT_H1_CHANNEL_CTRL_PIN;
    HAL_GPIO_Init(BAT_H1_CHANNEL_CTRL_PORT, &init);
    init.Pin = BAT_H2_CHANNEL_CTRL_PIN;
    HAL_GPIO_Init(BAT_H2_CHANNEL_CTRL_PORT, &init);

    HAL_GPIO_WritePin(BAT_H1_CHANNEL_CTRL_PORT, BAT_H1_CHANNEL_CTRL_PIN, GPIO_PIN_SET);
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
    HAL_GPIO_WritePin(VBAT_BAT_SENSE_CTRL_PORT, VBAT_BAT_SENSE_CTRL_PIN, GPIO_PIN_RESET);
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

void PowerManager::setChannelStates(bool h1_on, bool h2_on)
{
    if (h1_on)
    {
        HAL_GPIO_WritePin(BAT_H1_CHANNEL_CTRL_PORT, BAT_H1_CHANNEL_CTRL_PIN, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(BAT_H1_CHANNEL_CTRL_PORT, BAT_H1_CHANNEL_CTRL_PIN, GPIO_PIN_SET);
    }

    if (h2_on)
    {
        HAL_GPIO_WritePin(BAT_H2_CHANNEL_CTRL_PORT, BAT_H2_CHANNEL_CTRL_PIN, GPIO_PIN_RESET);
    }
    else
    {
        HAL_GPIO_WritePin(BAT_H2_CHANNEL_CTRL_PORT, BAT_H2_CHANNEL_CTRL_PIN, GPIO_PIN_SET);
    }
}

void PowerManager::requestSwitchTo(PowerBatteryId target)
{
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_switch_complete_ms) < POWER_MIN_DWELL_MS)
    {
        return;
    }
    pending_switch = target;
    switch_in_progress = true;
    switch_phase = 0;
}

void PowerManager::processSwitch()
{
    if (!switch_in_progress)
        return;

    const uint32_t now = HAL_GetTick();

    if (switch_phase == 0)
    {
        setChannelStates(false, false);
        switch_off_ms = now;
        switch_phase = 1;
        return;
    }

    if ((uint32_t)(now - switch_off_ms) < POWER_SWITCH_DEADTIME_MS)
    {
        return;
    }

    if (pending_switch == PowerBatteryId::H1)
    {
        setChannelStates(true, false);
        active_discharge = PowerBatteryId::H1;
    }
    else
    {
        setChannelStates(false, true);
        active_discharge = PowerBatteryId::H2;
    }
    switch_in_progress = false;
    last_switch_complete_ms = now;
    switch_phase = 0;
}

bool PowerManager::canUseAdcNow() const
{
    if (get_usb_mounted())
    {
        return false;
    }
    if (ADCManager::getInstance().getADCMode() == ADC_MODE_INPUT_CONTINUOUS ||
        ADCManager::getInstance().getADCMode() == ADC_MODE_CONTINUOUS)
    {
        return false;
    }
    return true;
}

void PowerManager::startVoltageMeasurementCycle()
{
    meas_cycle_start_ms = HAL_GetTick();
    meas_stage = MeasureStage::Setup;
    meas_target = MeasureTarget::H1;
    meas_stage_start_ms = meas_cycle_start_ms;
    adc_configured_for_batt = false;
}

void PowerManager::processVoltageMeasurement()
{
    const uint32_t now = HAL_GetTick();

    if (meas_stage == MeasureStage::Setup)
    {
        if (!configureAdc1ForBattery())
        {
            restoreAdc1ForButtons();
            meas_stage = MeasureStage::Idle;
            return;
        }
        adc_configured_for_batt = true;
        meas_stage = MeasureStage::WaitSettle;
        meas_stage_start_ms = now;
        HAL_GPIO_WritePin(VBAT_H1_SENSE_CTRL_PORT, VBAT_H1_SENSE_CTRL_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(VBAT_BAT_SENSE_CTRL_PORT, VBAT_BAT_SENSE_CTRL_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(VBAT_H2_SENSE_CTRL_PORT, VBAT_H2_SENSE_CTRL_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(VBAT_H1_SENSE_CTRL_PORT, VBAT_H1_SENSE_CTRL_PIN, GPIO_PIN_SET);
        return;
    }

    if (meas_stage == MeasureStage::WaitSettle)
    {
        if ((uint32_t)(now - meas_stage_start_ms) < POWER_MEAS_SETTLE_MS)
        {
            return;
        }
        meas_stage = MeasureStage::StartAdc;
    }

    if (meas_stage == MeasureStage::StartAdc)
    {
        if (!startSingleAdc())
        {
            restoreAdc1ForButtons();
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
                restoreAdc1ForButtons();
                meas_stage = MeasureStage::Idle;
            }
            return;
        }

        const uint32_t mv = rawToBattMv(adc_single_value);
        if (meas_target == MeasureTarget::H1)
        {
            h1_mv = mv;
            h1_soc = socFromMv(h1_mv);
            meas_target = MeasureTarget::Bat;
            HAL_GPIO_WritePin(VBAT_H1_SENSE_CTRL_PORT, VBAT_H1_SENSE_CTRL_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(VBAT_BAT_SENSE_CTRL_PORT, VBAT_BAT_SENSE_CTRL_PIN, GPIO_PIN_SET);
            meas_stage = MeasureStage::WaitSettle;
            meas_stage_start_ms = now;
            return;
        }
        if (meas_target == MeasureTarget::Bat)
        {
            bat_mv = mv;
            meas_target = MeasureTarget::H2;
            HAL_GPIO_WritePin(VBAT_BAT_SENSE_CTRL_PORT, VBAT_BAT_SENSE_CTRL_PIN, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(VBAT_H2_SENSE_CTRL_PORT, VBAT_H2_SENSE_CTRL_PIN, GPIO_PIN_SET);
            meas_stage = MeasureStage::WaitSettle;
            meas_stage_start_ms = now;
            return;
        }

        h2_mv = mv;
        h2_soc = socFromMv(h2_mv);

        HAL_GPIO_WritePin(VBAT_H2_SENSE_CTRL_PORT, VBAT_H2_SENSE_CTRL_PIN, GPIO_PIN_RESET);
        restoreAdc1ForButtons();

        last_voltage_update_ms = now;
        meas_stage = MeasureStage::Idle;
    }
}

bool PowerManager::configureAdc1ForBattery()
{
    if (ADCManager::getInstance().getADCMode() == ADC_MODE_LOW_LATENCY)
    {
        HAL_ADC_Stop_DMA(&hadc1);
    }

    hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
    hadc1.Init.Resolution = ADC_RESOLUTION_16B;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    hadc1.Init.OversamplingMode = DISABLE;

    if (HAL_ADC_Init(&hadc1) != HAL_OK)
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
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        return false;
    }

    return true;
}

void PowerManager::restoreAdc1ForButtons()
{
    MX_ADC1_Init();
    adc_configured_for_batt = false;
}

bool PowerManager::startSingleAdc()
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return false;
    }
    return true;
}

bool PowerManager::pollSingleAdcDone(uint32_t timeout_ms)
{
    const HAL_StatusTypeDef st = HAL_ADC_PollForConversion(&hadc1, timeout_ms);
    if (st == HAL_OK)
    {
        adc_single_value = HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
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
    if (mv <= 3000u) return 0.0f;
    if (mv >= 4200u) return 100.0f;

    if (mv < 3300u) return (float)(mv - 3000u) * (10.0f / 300.0f);
    if (mv < 3600u) return 10.0f + (float)(mv - 3300u) * (20.0f / 300.0f);
    if (mv < 3800u) return 30.0f + (float)(mv - 3600u) * (30.0f / 200.0f);
    if (mv < 4000u) return 60.0f + (float)(mv - 3800u) * (25.0f / 200.0f);
    return 85.0f + (float)(mv - 4000u) * (15.0f / 200.0f);
}
