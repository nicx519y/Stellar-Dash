#pragma once
#include "battery_policy.hpp"

// Sampled at telemetry cadence, not on the input/ISR path. Keep the inline
// policy compact even when its caller includes it before an optimization pragma.
#pragma GCC push_options
#pragma GCC optimize("Os")

// Runtime-only limits for a low-battery protection/recovery session only.
// Normal operation never evaluates supply strength or changes user lighting.
// ICHGR is charge-only; zero is NOT proof of zero battery discharge.
class PowerLoadPolicy {
public:
    void sample(uint32_t now, bool valid, bool present, bool qualified,
                bool protectedMode, uint16_t mv, uint16_t soc,
                uint16_t chargeMa, bool dpm, bool thermal, bool full) {
        const bool gap = sampled_ && now - lastSample_ > 2500u;
        sampled_ = true; lastSample_ = now;
        if (!valid || gap) resetEvidence();
        // Independent undervoltage protection remains active outside a session.
        if (valid && floor_.update(now, mv <= 3000u, 1000u)) trip();
        if (!engaged_ && !protectedMode) return;
        if (!engaged_) { limit(); resetEvidence(); }
        if (gap && engaged_) cap_ = 0;
        if (!valid) {
            if (engaged_ || protectedMode) {
                cap_ = 0;
                if (invalid_.update(now, true, 3000u)) trip();
            }
            return;
        }
        invalid_ = {};
        if (unplug_.update(now, !present && mv > 3100u, 1000u)) {
            locked_ = false;
            insufficient_ = false;
        }
        const bool charging = qualified && !dpm && !thermal &&
            (chargeMa >= 100u || (full && mv >= 4100u));
        // A failed trial still needs proven charging before another attempt.
        // Once awake with sustained reserve, end the session altogether. Do not
        // keep testing DPM/thermal/current after returning to normal operation.
        const bool canFinish = !protectedMode && !locked_;
        if (retry_.update(now, (locked_ ? charging : canFinish) &&
                          mv >= 3550u && soc >= 150u, 30000u)) {
            if (canFinish) { *this = PowerLoadPolicy{}; return; }
            locked_ = false;
            insufficient_ = false;
            resetEvidence();
        }
        if (locked_) { cap_ = 0; return; }
        if (protectedMode) { limit(); resetEvidence(); return; }
        if (!qualified) {
            healthy_ = {}; weak_ = {}; depleted_ = {}; trend_ = false;
            if (engaged_) cap_ = 0;
            return;
        }
        if (!trend_) { trend_ = true; trendAt_ = now; trendMv_ = mv; }
        bool falling = false;
        if (now - trendAt_ >= 10000u) {
            const uint16_t drop = dpm ? 30u : 60u;
            falling = chargeMa < 100u && uint32_t(mv) + drop <= trendMv_;
            trendAt_ = now; trendMv_ = mv;
        }
        const bool constrained = (dpm || thermal) && chargeMa < 100u;
        const bool weak = weak_.update(now, constrained, 3000u);
        if (falling || weak) {
            insufficient_ = true;
            if (cap_ != 0u) {
                limit(); resetEvidence();
                return; // Give shedding a full new observation window.
            }
            if (falling) { trip(); return; }
        }
        // At low voltage, do not wait for a further fall on a saturated source.
        if (depleted_.update(now, engaged_ && cap_ == 0u && constrained && mv <= 3200u, 5000u)) {
            trip(); return;
        }
        if (!engaged_) return;
        if (healthy_.update(now, charging && mv >= 3100u && !falling,
                            cap_ == 0u ? 30000u : 10000u)) {
            cap_ = cap_ >= 75u ? 100u : uint8_t(cap_ + 25u);
            insufficient_ = false;
            healthy_ = {}; trend_ = false;
        }
    }
    uint8_t ledPercent() const { return cap_; }
    uint8_t lcdPercent() const { return uint8_t(10u + uint16_t(cap_) * 90u / 100u); }
    bool insufficient() const { return insufficient_; }
    bool locked() const { return locked_; }
    bool recoveryAllowed(uint16_t mv) const { return !locked_ && mv >= 3100u; }
private:
    void limit() { engaged_ = true; cap_ = 0; healthy_ = {}; }
    void trip() { limit(); locked_ = insufficient_ = true; }
    void resetEvidence() {
        healthy_ = {}; weak_ = {}; depleted_ = {}; floor_ = {};
        unplug_ = {}; retry_ = {}; trend_ = false;
    }
    BatteryConditionTimer healthy_, weak_, depleted_, floor_, unplug_, retry_, invalid_;
    uint32_t lastSample_ = 0, trendAt_ = 0;
    uint16_t trendMv_ = 0;
    uint8_t cap_ = 100;
    bool sampled_ = false, trend_ = false, engaged_ = false;
    bool locked_ = false, insufficient_ = false;
};
#pragma GCC pop_options
