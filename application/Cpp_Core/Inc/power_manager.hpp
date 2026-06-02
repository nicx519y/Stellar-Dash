#ifndef POWER_MANAGER_HPP
#define POWER_MANAGER_HPP

#include <stdint.h>

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

    bool isCharging() const;
    bool isFastCharging() const;
    uint32_t getBatteryVoltageMv() const;
    float getBatterySocPercent() const;

    void configureWakeup();
    void enterStopSleep();
    void enterStandbySleep();

private:
    PowerManager() = default;

    void configureGpios();
    bool readVbusPresent() const;
    bool readFastChargingPin() const;
    bool readPowerButtonPressed() const;
    uint32_t rawToBattMv(uint32_t raw) const;
    float socFromMv(uint32_t mv) const;
    void requestStandbySleep(const char* reason);

    uint32_t last_mode_poll_ms = 0;
    uint32_t last_status_log_ms = 0;
    uint32_t power_button_pressed_since_ms = 0;
    bool vbus_present = false;
    bool fast_charging_pin_high = false;
    bool standby_sleep_in_progress = false;
};

#define POWER_MANAGER PowerManager::getInstance()

#endif
