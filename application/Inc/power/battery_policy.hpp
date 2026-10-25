#pragma once
#include <stdint.h>

enum class BatteryLevel : uint8_t { Normal, Low, Critical, RecoveryReady };

// A timer is valid only while fresh, consecutive measurements confirm it.
struct BatteryConditionTimer {
    uint32_t since = 0;
    bool active = false;
    bool update(uint32_t now, bool condition, uint32_t duration) {
        if (!condition) { active = false; return false; }
        if (!active) { active = true; since = now; }
        return now - since >= duration;
    }
};

class BatteryPolicy {
public:
    uint32_t confirmationMask() const {
        return uint32_t(warning_.active) | (uint32_t(critical_.active)<<1) |
               (uint32_t(normal_.active)<<2) | (uint32_t(recovery_.active)<<3);
    }
    void sample(uint32_t now, bool valid, uint16_t mv, uint16_t soc, bool chargeAllowed,
                bool forceProtection = false) {
        if (forceProtection) { protected_ = true; ready_ = false; }
        if (!valid || (haveSample_ && now - lastSample_ > 2500u)) resetTimers();
        lastSample_ = now;
        haveSample_ = true;
        if (!valid) { ready_ = false; return; }
        // SOC is the calibrated operational capacity from PowerManager.
        if (warning_.update(now, soc <= 100u, 10000u)) low_ = true;
        if (normal_.update(now, soc >= 150u, 30000u)) low_ = false;
        // A qualified external supply can run the device while the cell charges.
        // Loss of that supply restarts the normal undervoltage confirmation.
        if (critical_.update(now, !chargeAllowed && mv <= 3200u, 3000u)) protected_ = true;
        // The caller applies fresh telemetry, qualified VBUS, charger health
        // and the load policy's floor/retry gates. No extra 3.55V/10s delay here.
        ready_ = recovery_.update(now, protected_ && chargeAllowed && !forceProtection, 0u);
    }
    BatteryLevel level() const {
        return protected_ ? (ready_ ? BatteryLevel::RecoveryReady : BatteryLevel::Critical) :
            (low_ ? BatteryLevel::Low : BatteryLevel::Normal);
    }
    bool needsFrequentSamples() const {
        return low_ || protected_ || warning_.active || critical_.active;
    }
    bool acknowledgeRecovery() {
        if (!ready_) return false;
        protected_ = ready_ = false;
        resetTimers();
        return true;
    }
private:
    void resetTimers() { warning_ = {}; normal_ = {}; critical_ = {}; recovery_ = {}; }
    BatteryConditionTimer warning_, normal_, critical_, recovery_;
    uint32_t lastSample_ = 0;
    bool haveSample_ = false, low_ = false, protected_ = false, ready_ = false;
};

// Wake requires a new press after readiness, followed by release. Held keys,
// readiness loss and the press used to inspect a depleted battery cannot unlock.
class BatteryRecoveryButton {
public:
    bool update(bool ready, bool down) {
        if (!ready) { armed_ = pressed_ = false; return false; }
        if (!armed_) { if (!down) armed_ = true; return false; }
        if (down) { pressed_ = true; return false; }
        const bool release = pressed_;
        pressed_ = false;
        return release;
    }
private:
    bool armed_ = false, pressed_ = false;
};
