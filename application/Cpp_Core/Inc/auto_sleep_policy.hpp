#pragma once
#include <stdint.h>

// Pure runtime policy. No retained state, storage, clocks or peripheral access.
class AutoSleepPolicy {
public:
    enum class State { Active, Preparing, Sleeping, Restoring };
    static constexpr uint32_t bootGuardMs = 30000u;
    static constexpr uint32_t prepareLimitMs = 500u;
    static constexpr uint32_t restoreLimitMs = 100u;

    void initialize(uint32_t now, bool enabled) {
        *this = AutoSleepPolicy{};
        enabled_ = enabled;
        boot_ = lastActivity_ = since_ = now;
    }
    void inhibit() { enabled_ = false; }
    bool enabled() const { return enabled_; }
    State state() const { return state_; }
    uint32_t elapsed(uint32_t now) const { return now - since_; }
    void activity(uint32_t now) { lastActivity_ = now; }
    bool shouldPrepare(uint32_t now, uint32_t timeout, bool eligible) {
        if (!eligible || !wasEligible_) lastActivity_ = now;
        wasEligible_ = eligible;
        return enabled_ && eligible && state_ == State::Active && timeout != 0u &&
               now - boot_ >= bootGuardMs && now - lastActivity_ >= timeout;
    }
    void transition(State next, uint32_t now) { state_ = next; since_ = now; }
    bool timedOut(uint32_t now) const {
        return (state_ == State::Preparing && elapsed(now) >= prepareLimitMs) ||
               (state_ == State::Restoring && elapsed(now) >= restoreLimitMs);
    }
    void active(uint32_t now) { transition(State::Active, now); activity(now); }
private:
    bool enabled_ = false;
    bool wasEligible_ = false;
    State state_ = State::Active;
    uint32_t boot_ = 0, lastActivity_ = 0, since_ = 0;
};

// Capture the initial held keys, then suppress only those keys until release.
class SleepReleaseGate {
public:
    void arm(uint32_t held) { blocked_ |= held; capture_ = true; }
    uint32_t filter(uint32_t mask) {
        if (capture_) { blocked_ |= mask; capture_ = false; }
        blocked_ &= mask;
        return mask & ~blocked_;
    }
    bool pending() const { return capture_ || blocked_ != 0u; }
private:
    uint32_t blocked_ = 0;
    bool capture_ = false;
};

// ISR-owned debounce, using the 1 ms tick rather than a sleeping CPU's DWT.
class SleepWakeKeys {
public:
    void sample(uint32_t raw) {
        if (raw != candidate_) { candidate_ = raw; count_ = 1; }
        else if (count_ < 5) { ++count_; }
        if (count_ == 5) {
            pressed_ |= candidate_ & ~stable_;
            stable_ = candidate_;
        }
    }
    uint32_t stable() const { return stable_; }
    uint32_t takePressed() { uint32_t p = pressed_; pressed_ = 0; return p; }
private:
    uint32_t candidate_ = 0, stable_ = 0, pressed_ = 0;
    uint8_t count_ = 0;
};
