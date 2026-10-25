#ifndef POWER_MANAGER_HPP
#define POWER_MANAGER_HPP

#include <stdint.h>
#include "charge_recovery_policy.hpp"
#include "battery_policy.hpp"
#include "power_load_policy.hpp"
#include "battery_soc_estimator.hpp"

extern "C" {
#include "bq25895.h"
#include "max17048.h"
}

enum class PowerChargeState : uint8_t {
    Discharging = 0,
    Charging = 1,
    Full = 2,
    Fault = 3,
    Unknown = 4,
};

enum class PowerDataQuality : uint8_t { Unknown, Fresh, Stale };
enum class PowerChargeBlock : uint8_t {
    None, NoInput, Telemetry, Profile, InputRange, ChargerFault, FaultRecovery, BoardFault
};

/*
 * Kept for source and wire compatibility with the frozen RF path.  The latest
 * PCB is a single 1S2P pack, so H1 is always selected and H2 is always zero.
 */
enum class PowerBatteryId : uint8_t {
    H1 = 0,
    H2 = 1,
};

struct PowerBatteryVoltages {
    uint32_t h1_mv;
    uint32_t h2_mv;
    uint32_t bat_mv;
};

enum PowerFaultBits : uint16_t {
    POWER_FAULT_BQ_RAW_MASK = 0x00FFu,
    POWER_FAULT_CHARGER_OFFLINE = 1u << 8,
    POWER_FAULT_GAUGE_OFFLINE = 1u << 9,
    POWER_FAULT_PROFILE_INVALID = 1u << 10,
    POWER_FAULT_VBUS_OUT_OF_RANGE = 1u << 11,
};

struct PowerSnapshot {
    static constexpr uint16_t lowBatterySocPermille = 100u;
    uint16_t cell_mv = 0;
    uint16_t soc_permille = 0;
    uint16_t vbus_mv = 0;
    uint16_t charge_current_ma = 0;
    uint16_t input_current_limit_ma = 0;
    uint16_t fault_bits = POWER_FAULT_CHARGER_OFFLINE | POWER_FAULT_GAUGE_OFFLINE;
    PowerChargeState charge_state = PowerChargeState::Unknown;
    PowerDataQuality data_quality = PowerDataQuality::Unknown;
    PowerChargeBlock charge_block = PowerChargeBlock::Telemetry;
    BatteryLevel battery_level = BatteryLevel::Normal;
    bool battery_confirmation_pending = false;
    BatterySocSource soc_source = BatterySocSource::ColdEstimate;
    bool soc_estimated = true;
    uint32_t sampled_at_ms = 0;
    bool has_sample = false;
    bool thermal_regulation = false;
    bool vbus_present = false;
    bool fast_charge = false;
    bool gauge_online = false;
    bool charger_online = false;
    bool input_current_regulation = false;
    bool input_voltage_regulation = false;
    bool valid = false;
    uint8_t led_power_percent = 100;
    uint8_t lcd_power_percent = 100;
    bool supply_insufficient = false;
    bool supply_recovery_locked = false;
    uint16_t raw_soc_permille = 0; // MAX17048 diagnostic; never a business threshold.

    // Battery-only healthy standby can defer telemetry. Unknown, low battery,
    // external power and faults retain the normal one-second maintenance rate.
    uint32_t sleepMaintenanceIntervalMs() const {
        return valid && gauge_online && charger_online && fault_bits == 0u &&
               battery_level == BatteryLevel::Normal && !battery_confirmation_pending &&
               !vbus_present && vbus_mv < 3900u && soc_permille > lowBatterySocPermille &&
               charge_state == PowerChargeState::Discharging ? 5000u : 1000u;
    }
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

    PowerSnapshot getSnapshot() const;
    PowerChargeState getChargeState() const;
    float getTotalSocPercent() const;
    PowerBatteryVoltages getVoltages() const;
    PowerBatteryId getActiveDischargeBattery() const;
    bool isVoltageValid() const;
    bool isFastCharging() const;
    bool isLowBattery() const;
    bool isPolling() const;
    bool prepareSystemSleep();
    bool restoreSystemWake();

    void notifyChargerIrqFromISR();
    void notifyGaugeAlertFromISR();
    void requestRefreshFromISR();
    bool batteryProtectionRequired() const;
    bool acknowledgeBatteryRecovery();

private:
    PowerManager() = default;

    void configureSafetyGpios();
    bool initializeDevices();
    void refreshSnapshot(bool clearGaugeAlert);
    void publishSnapshot(const BQ25895_State& charger_state,
                         const MAX17048_State& gauge_state,
                         bool charger_ok, bool gauge_ok);
    void stepPoll(uint32_t now);
    void beginChargerJob(BQ25895_JobKind kind, BQ25895_InputProfile profile);
    void beginGaugeJob(MAX17048_JobKind kind);
    void processLowVoltageProtection();
    void invalidateSample();
    void setChargingEnabled(bool enabled);
    bool isFastChargeDetected() const;
    uint32_t consumeIrqFlags();

    BQ25895_Handle charger_{};
    MAX17048_Handle gauge_{};
    ChargeRecoveryPolicy charge_recovery_{};
    BatteryPolicy battery_policy_{};
    PowerLoadPolicy load_policy_{};
    BatterySocEstimator soc_estimator_{};
    PowerSnapshot snapshot_{};

    enum class PollPhase : uint8_t {
        Idle, InitCharger, WaitAdc, ReadCharger, InitGauge, ReadGauge,
        ClearGauge, Profile, ConfigureProfile, VerifyProfile, Publish
    };
    PollPhase poll_phase_ = PollPhase::Idle;
    BQ25895_Job charger_job_{};
    MAX17048_Job gauge_job_{};
    BQ25895_State poll_charger_{};
    MAX17048_State poll_gauge_{};
    uint32_t pending_irq_flags_ = 0;
    uint32_t poll_started_ms_ = 0;
    uint32_t adc_started_ms_ = 0;
    uint32_t adc_poll_ms_ = 0;
    uint8_t poll_fault_ = 0;
    bool poll_charger_ok_ = false;
    bool poll_gauge_ok_ = false;
    bool poll_clear_gauge_ = false;
    bool poll_recovery_ = false;
    bool profile_repair_attempted_ = false;

    volatile uint32_t irq_flags_ = 0;
    uint32_t last_poll_ms_ = 0;
    uint32_t last_profile_check_ms_ = 0;
    uint32_t last_reinitialize_ms_ = 0;
    bool profile_valid_ = false;
    bool charging_enabled_ = false;
};

#define POWER_MANAGER PowerManager::getInstance()

#ifdef __cplusplus
extern "C" {
#endif

/* Called by the shared EXTI handlers; these functions never access I2C. */
void PowerManager_NotifyChargerIrqFromISR(void);
void PowerManager_NotifyGaugeAlertFromISR(void);
void PowerManager_RequestRefreshFromISR(void);

#ifdef __cplusplus
}
#endif

#endif
