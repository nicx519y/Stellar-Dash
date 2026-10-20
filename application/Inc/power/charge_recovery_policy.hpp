#pragma once
#include <stdint.h>

// Runtime-only charger fault memory. A REG0C read clears history in the chip,
// so a subsequent clean read alone must not restart an abnormal charge cycle.
class ChargeRecoveryPolicy {
public:
    void record(uint8_t fault, uint8_t latchedFault) {
        if ((latchedFault & ~latched_) != 0u) unplugTiming_ = false;
        latched_ |= latchedFault;
        if (fault != 0u) {
            recovering_ = true;
            cleanTiming_ = false;
        }
    }
    void sample(uint32_t now, bool chargerValid, bool inputAbsent,
                bool healthy, uint8_t fault, uint8_t latchedFault) {
        record(fault, latchedFault);
        if (haveSample_ && now - lastSample_ > 2500u) {
            unplugTiming_ = cleanTiming_ = false;
        }
        haveSample_ = true;
        lastSample_ = now;
        if (chargerValid && inputAbsent) {
            if (!unplugTiming_) { unplugTiming_ = true; unplugAt_ = now; }
            if (now - unplugAt_ >= 1000u) latched_ = 0u;
        } else {
            unplugTiming_ = false;
        }
        if (healthy && fault == 0u && latched_ == 0u) {
            if (!cleanTiming_) { cleanTiming_ = true; cleanAt_ = now; }
            if (now - cleanAt_ >= 10000u) recovering_ = false;
        } else {
            cleanTiming_ = false;
        }
    }
    uint8_t latchedFault() const { return latched_; }
    bool blocked() const { return latched_ != 0u || recovering_; }
private:
    uint32_t unplugAt_ = 0, cleanAt_ = 0, lastSample_ = 0;
    uint8_t latched_ = 0;
    bool unplugTiming_ = false, cleanTiming_ = false, recovering_ = false;
    bool haveSample_ = false;
};
