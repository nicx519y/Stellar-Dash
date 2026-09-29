#pragma once
#include <stdint.h>

// One physical reader owns the line. An observed release remains evidence even
// after the peer asserts the next event; it is never inferred from a falling edge.
class Ch585ReleaseState {
public:
    enum class Phase : uint8_t { Idle, Reading, Waiting, Fault };
    void reset() { phase = Phase::Idle; released = false; }
    bool begin() {
        if (phase != Phase::Idle) return false;
        released = false; phase = Phase::Reading; return true;
    }
    void rising() {
        if (phase == Phase::Reading || phase == Phase::Waiting) released = true;
    }
    void finish(uint32_t now) {
        if (phase == Phase::Reading) { since = now; phase = Phase::Waiting; }
    }
    Phase poll(bool high, uint32_t now, uint32_t timeout) {
        if (phase == Phase::Waiting) {
            if (released || high) phase = Phase::Idle;
            else if (static_cast<uint32_t>(now - since) >= timeout) phase = Phase::Fault;
        }
        return phase;
    }
    Phase phase = Phase::Idle;
    uint32_t since = 0;
private:
    bool released = false;
};
