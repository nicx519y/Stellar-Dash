#ifndef POWER_MANAGER_HPP
#define POWER_MANAGER_HPP

#include <stdint.h>

#include "adc.h"

enum class PowerChargeState : uint8_t {
    Discharging = 0,
    Charging = 1,
    Full = 2,
    Fault = 3,
    Unknown = 4,
};

enum class PowerBatteryId : uint8_t {
    H1 = 0,
    H2 = 1,
};

struct PowerBatteryVoltages {
    uint32_t h1_mv;
    uint32_t h2_mv;
    uint32_t bat_mv;
};

class PowerManager {
public:
    PowerManager(PowerManager const&) = delete;
    void operator=(PowerManager const&) = delete;
    static PowerManager& getInstance() {
        static PowerManager instance;
        return instance;
    }

    void setup();
    void loop();

    PowerChargeState getChargeState() const;
    float getTotalSocPercent() const;
    PowerBatteryVoltages getVoltages() const;
    PowerBatteryId getActiveDischargeBattery() const;
    bool isFastCharging() const;
    bool isLowBattery() const;
    bool prepareSystemSleep();
    bool restoreSystemWake();

private:
    PowerManager() = default;

    void configureGpios();
    bool isVbusPresent() const;
    bool isFastChargeDetected() const;
    void setChannelStates(bool h1_on, bool h2_on);
    void requestSwitchTo(PowerBatteryId target);
    void processSwitch();

    bool canUseAdcNow() const;
    void startVoltageMeasurementCycle();
    void processVoltageMeasurement();
    void processLowVoltageProtection();
    bool configureAdcForBattery();
    void restoreAdcForButtons();
    bool startSingleAdc();
    bool pollSingleAdcDone(uint32_t timeout_ms);
    uint32_t rawToBattMv(uint32_t raw) const;
    float socFromMv(uint32_t mv) const;

    enum class MeasureStage : uint8_t { Idle, Setup, WaitSettle, StartAdc, WaitAdc, Done };
    enum class MeasureTarget : uint8_t { H1, Bat, H2 };

    uint32_t last_mode_poll_ms = 0;
    bool vbus_present = false;
    bool fast_charging = false;
    PowerBatteryId active_discharge = PowerBatteryId::H1;
    PowerBatteryId pending_switch = PowerBatteryId::H1;
    bool switch_in_progress = false;
    uint32_t last_switch_complete_ms = 0;
    uint32_t switch_off_ms = 0;
    uint8_t switch_phase = 0;

    MeasureStage meas_stage = MeasureStage::Idle;
    MeasureTarget meas_target = MeasureTarget::H1;
    uint32_t meas_stage_start_ms = 0;
    uint32_t meas_cycle_start_ms = 0;
    bool adc_configured_for_batt = false;
    ADC_SamplingMode adc_mode_before_batt = ADC_MODE_LOW_LATENCY;
    uint32_t adc_single_value = 0;

    uint32_t h1_mv = 0;
    uint32_t h2_mv = 0;
    uint32_t bat_mv = 0;
    float h1_soc = 0.0f;
    float h2_soc = 0.0f;
    bool voltage_valid = false;

    uint32_t last_voltage_update_ms = 0;
    uint8_t switch_confirm_count = 0;
    uint8_t low_sleep_confirm_count = 0;
};

#define POWER_MANAGER PowerManager::getInstance()

#endif
