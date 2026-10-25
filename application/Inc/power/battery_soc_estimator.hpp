#pragma once
#include <stdint.h>
#include "battery_soc_curve.hpp"
#pragma GCC push_options
#pragma GCC optimize("Os")

enum class BatterySocSource : uint8_t { DischargeCurve, ChargeIntegral, ColdEstimate, ConfirmedFull };

// Runtime only. 7411mAh is the observed near-empty/near-full charge reference,
// not a measured nameplate capacity. See docs/data/charge-soc-20261025.json.
// BQ25895 ICHGR is charge-only; never infer discharge current from zero ICHGR.
class BatterySocEstimator {
public:
    static constexpr uint32_t referenceMah = 7411u;
    static constexpr uint32_t maxChargeGapMs = 2500u;
    static constexpr uint32_t fullConfirmMs = 30000u;
    struct Sample {
        uint32_t at;
        uint16_t mv, rawSoc, chargeMa;
        bool valid, input, charging, full, allowed, weak;
    };
    void sample(const Sample& s) {
        if (!s.valid) {
            estimated_ = true; previousValid_ = false; fullWaiting_ = false;
            batteryWaiting_ = false; return;
        }
        if (initialized_ && s.at == lastAt_) return;
        const uint32_t dt = s.at - lastAt_;
        const bool consecutive = previousValid_ && dt <= maxChargeGapMs;
        if (!initialized_) {
            initialized_ = true;
            soc_ = s.input ? clamp(s.rawSoc) : BatterySoc_FromMillivolts(s.mv);
            reanchor_ = s.input;
            estimated_ = true; // Every cold start lacks retained history.
            source_ = s.input ? BatterySocSource::ColdEstimate : BatterySocSource::DischargeCurve;
            if (s.input && soc_ > 990u) soc_ = 990u;
        } else if (s.input) {
            batteryWaiting_ = false;
            if (soc_ > 990u && source_ != BatterySocSource::ConfirmedFull) soc_ = 990u;
            if (!previousInput_) slewRemainder_ = 0;
            if (!consecutive) { estimated_ = true; fullWaiting_ = false; }
            if (s.allowed && s.charging) {
                if (consecutive && previousInput_ && previousCharging_) {
                    // Trapezoidal mA*ms * 1000 / (2 * mAh * 3600000).
                    // Cancel 1000 to keep the retained numerator small.
                    chargeRemainder_ += uint64_t(previousMa_ + s.chargeMa) * dt;
                    constexpr uint64_t divisor = uint64_t(referenceMah) * 7200u;
                    const uint32_t increment = uint32_t(chargeRemainder_ / divisor);
                    chargeRemainder_ %= divisor;
                    soc_ = uint16_t(uint32_t(soc_) + increment > 990u ?
                        (source_ == BatterySocSource::ConfirmedFull ? 1000u : 990u) : soc_ + increment);
                    if (source_ != BatterySocSource::ConfirmedFull) source_ = BatterySocSource::ChargeIntegral;
                }
            } else if (!s.full || !s.allowed) {
                estimated_ = true;
            }
            // During a weak supply, only downward voltage evidence is used.
            if (s.weak && (!s.charging || s.chargeMa == 0u)) {
                estimated_ = true;
                if (consecutive) slewDown(BatterySoc_FromMillivolts(s.mv), dt);
            }
        } else {
            fullWaiting_ = false;
            if (previousInput_) source_ = BatterySocSource::DischargeCurve;
            if (previousValid_ && dt > 6000u) estimated_ = true;
            if (!batteryWaiting_ || previousInput_ || !previousValid_ || dt > 6000u) {
                batteryWaiting_ = true; batterySince_ = s.at; slewRemainder_ = 0;
            }
            const uint32_t stable = s.at - batterySince_;
            const auto target = BatterySoc_FromMillivolts(s.mv);
            if (previousValid_ && dt <= 6000u && stable >= 10000u) {
                // Normally never rise on battery rebound. An explicitly
                // uncertain cold start may reanchor after 30s on battery.
                if (reanchor_ && stable >= 30000u && target > soc_) {
                    const uint16_t step = slewStep(dt);
                    soc_ = target - soc_ <= step ? target : uint16_t(soc_ + step);
                } else slewDown(target, stable-10000u < dt ? stable-10000u : dt);
            }
            if (stable >= 30000u && (!reanchor_ || target == soc_)) {
                reanchor_ = estimated_ = false;
                source_ = BatterySocSource::DischargeCurve;
            }
        }
        if (s.input && s.allowed && s.full) {
            if (!fullWaiting_ || !consecutive || !previousInput_) {
                fullWaiting_ = true; fullSince_ = s.at;
            } else if (s.at - fullSince_ >= fullConfirmMs) {
                soc_ = 1000u; estimated_ = reanchor_ = false;
                source_ = BatterySocSource::ConfirmedFull; chargeRemainder_ = 0;
            }
        } else fullWaiting_ = false;
        previousInput_ = s.input;
        previousCharging_ = s.allowed && s.charging;
        previousMa_ = s.chargeMa;
        previousValid_ = true; lastAt_ = s.at;
    }
    void anchorEmpty() {
        if (!initialized_) return;
        soc_ = 0; chargeRemainder_ = 0; estimated_ = reanchor_ = false;
        source_ = BatterySocSource::DischargeCurve;
        previousCharging_ = false; fullWaiting_ = false;
    }
    uint16_t soc() const { return soc_; }
    bool estimated() const { return estimated_; }
    BatterySocSource source() const { return source_; }
private:
    static uint16_t clamp(uint16_t n) { return n > 1000u ? 1000u : n; }
    uint16_t slewStep(uint32_t dt) {
        slewRemainder_ += dt;
        const uint16_t step = uint16_t(slewRemainder_ / 1000u);
        slewRemainder_ %= 1000u;
        return step; // 1 permille/s = 0.1 percentage point/s
    }
    void slewDown(uint16_t target, uint32_t dt) {
        const uint16_t step = slewStep(dt);
        if (target < soc_) soc_ = soc_ - target <= step ? target : uint16_t(soc_ - step);
    }
    uint64_t chargeRemainder_ = 0;
    uint32_t lastAt_ = 0, batterySince_ = 0, fullSince_ = 0, slewRemainder_ = 0;
    uint16_t soc_ = 0, previousMa_ = 0;
    BatterySocSource source_ = BatterySocSource::ColdEstimate;
    bool initialized_ = false, estimated_ = true, reanchor_ = false;
    bool previousValid_ = false, previousInput_ = false, previousCharging_ = false;
    bool batteryWaiting_ = false, fullWaiting_ = false;
};
#pragma GCC pop_options
