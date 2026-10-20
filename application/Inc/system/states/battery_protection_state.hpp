#pragma once
#include "base_state.hpp"
#include "battery_policy.hpp"

class BatteryProtectionState : public BaseState {
public:
    static BatteryProtectionState& instance() { static BatteryProtectionState s; return s; }
    bool enter() override;
    void tick() override;
    void exit() override;
    void idle();
private:
    BatteryRecoveryButton button;
    uint32_t buttonChangedAt = 0;
    bool rawDown = false, stableDown = false, loadsStopped = false;
};
#define BATTERY_PROTECTION_STATE BatteryProtectionState::instance()
