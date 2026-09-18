#ifndef POWER_MANAGER_HPP
#define POWER_MANAGER_HPP

#include <stdint.h>

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
    uint16_t cell_mv = 0;
    uint16_t soc_permille = 0;
    uint16_t vbus_mv = 0;
    uint16_t charge_current_ma = 0;
    uint16_t input_current_limit_ma = 0;
    uint16_t fault_bits = POWER_FAULT_CHARGER_OFFLINE | POWER_FAULT_GAUGE_OFFLINE;
    PowerChargeState charge_state = PowerChargeState::Unknown;
    bool vbus_present = false;
    bool fast_charge = false;
    bool gauge_online = false;
    bool charger_online = false;
    bool input_current_regulation = false;
    bool input_voltage_regulation = false;
    bool valid = false;
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
    bool prepareSystemSleep();
    bool restoreSystemWake();

    void notifyChargerIrqFromISR();
    void notifyGaugeAlertFromISR();

private:
    PowerManager() = default;

    void configureSafetyGpios();
    bool initializeDevices();
    void refreshSnapshot(bool clearGaugeAlert);
    void processLowVoltageProtection();
    void setChargingEnabled(bool enabled);
    bool isFastChargeDetected() const;
    uint32_t consumeIrqFlags();

    BQ25895_Handle charger_{};
    MAX17048_Handle gauge_{};
    PowerSnapshot snapshot_{};

    volatile uint32_t irq_flags_ = 0;
    uint32_t last_poll_ms_ = 0;
    uint32_t last_profile_check_ms_ = 0;
    uint32_t last_reinitialize_ms_ = 0;
    uint8_t low_sleep_confirm_count_ = 0;
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

#ifdef __cplusplus
}
#endif

#endif
