#ifndef BOARD_POWER_HPP
#define BOARD_POWER_HPP

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Re-asserts PI4 without changing any other rail.  The bootloader leaves PI4
 * high before jumping to the XIP application; this hook preserves that state
 * as the application runtime is brought up.
 */
void BoardPower_EarlyMainHold(void);

/* Configures all board power-control pins to their fail-safe defaults. */
void BoardPower_Initialize(void);
void BoardPower_SetChargeEnabled(bool enabled);
void BoardPower_SetHallEnabled(bool enabled);
void BoardPower_EnableHallForAdc(void);
void BoardPower_SetKeyLedEnabled(bool enabled);
void BoardPower_SetAmbientLedEnabled(bool enabled);
void BoardPower_SetLcdEnabled(bool enabled);

#ifdef __cplusplus
}

class BoardPower {
public:
    BoardPower(const BoardPower&) = delete;
    BoardPower& operator=(const BoardPower&) = delete;

    static BoardPower& getInstance()
    {
        static BoardPower instance;
        return instance;
    }

    void setup();
    void assertMainPowerHold();
    void enterSafeState();
    void prepareForStandby();
    void releaseSafeState();

    void setChargeEnabled(bool enabled);
    void setHallEnabled(bool enabled);
    void setKeyLedEnabled(bool enabled);
    void setAmbientLedEnabled(bool enabled);
    void setLcdEnabled(bool enabled);
    void setCh585Enabled(bool enabled);
    bool setUsbHostEnabled(bool enabled);

    bool isInitialized() const { return initialized; }
    bool isChargeEnabled() const { return chargeEnabled; }
    bool isHallEnabled() const { return hallEnabled; }
    bool isKeyLedEnabled() const { return keyLedEnabled; }
    bool isAmbientLedEnabled() const { return ambientLedEnabled; }
    bool isLcdEnabled() const { return lcdEnabled; }
    bool isCh585Enabled() const { return ch585Enabled; }
    bool isUsbHostEnabled() const { return usbHostEnabled; }
    bool isSafeLatched() const { return safeLatched; }

private:
    BoardPower() = default;

    void setLedBoostEnabled(bool enabled);
    void refreshLedBoost();

    bool initialized = false;
    bool chargeEnabled = false;
    bool hallEnabled = false;
    bool ledBoostEnabled = false;
    bool keyLedEnabled = false;
    bool ambientLedEnabled = false;
    bool lcdEnabled = false;
    bool ch585Enabled = false;
    bool usbHostEnabled = false;
    bool safeLatched = true;
};

#define BOARD_POWER BoardPower::getInstance()
#endif

#endif
