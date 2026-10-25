#pragma once
#include <stdint.h>

// Snapshot held keys under PRIMASK before arming falling-edge wakeups. In
// protection only that existing level is ignored; a new key or pending edge
// still cancels WFI. Ordinary idle sleep continues to require released keys.
class StopWakeGate {
public:
    StopWakeGate(uint32_t heldAtEntry, bool protection)
        : ignoredHeld_(protection ? heldAtEntry : 0u) {}
    bool mayWait(uint32_t held, uint32_t pending, uint32_t captured, bool timerReady) const {
        return !(held & ~ignoredHeld_) && !pending && !captured && timerReady;
    }
private:
    uint32_t ignoredHeld_;
};
