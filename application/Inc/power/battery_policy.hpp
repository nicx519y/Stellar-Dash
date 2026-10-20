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
    void sample(uint32_t now, bool valid, uint16_t mv, uint16_t soc, bool chargeAllowed) {
        if (!valid || (haveSample_ && now - lastSample_ > 2500u)) resetTimers();
        lastSample_ = now;
        haveSample_ = true;
        if (!valid) { ready_ = false; return; }
        if (warning_.update(now, soc <= 100u || mv < 3450u, 10000u)) low_ = true;
        if (normal_.update(now, soc >= 150u && mv >= 3550u, 30000u)) low_ = false;
        if (critical_.update(now, mv <= 3200u, 3000u)) protected_ = true;
        ready_ = recovery_.update(now, protected_ && chargeAllowed && mv >= 3550u, 10000u);
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
