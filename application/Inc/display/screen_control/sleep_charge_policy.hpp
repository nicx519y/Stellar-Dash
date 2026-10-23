#pragma once
#include "power_manager.hpp"

// Display cadence only. Charger polling and battery protection keep their own clocks.
class SleepChargePolicy {
public:
    enum class Status : uint8_t { Wait, Charging, Fast, Full, Error };
    void update(uint32_t now, bool eligible, const PowerSnapshot& power) {
        const bool present = power.charger_online ? power.vbus_present :
            (visible_ && power.has_sample && now-power.sampled_at_ms<=10000u);
        const bool next = eligible && present;
        if (!next) { visible_=dirty_=false; return; }
        const bool invalid = !power.valid;
        const bool fault = power.charge_state==PowerChargeState::Fault ||
            power.charge_block==PowerChargeBlock::ChargerFault ||
            power.charge_block==PowerChargeBlock::BoardFault;
        // Never leave a plausible number or charging claim visible after a fault.
        if (!visible_ || now-updatedAt_>=5000u || (invalid && soc_>=0) ||
            (fault && status_!=Status::Error)) {
            soc_=invalid ? -1 : (power.soc_permille>=1000u ? 100 : power.soc_permille/10u);
            status_=fault ? Status::Error :
                invalid || power.charge_block!=PowerChargeBlock::None ? Status::Wait :
                power.charge_state==PowerChargeState::Full ? Status::Full :
                power.charge_state==PowerChargeState::Charging ?
                    (power.fast_charge ? Status::Fast : Status::Charging) : Status::Wait;
            updatedAt_=now; dirty_=true;
        }
        visible_=true;
    }
    bool visible() const { return visible_; }
    bool dirty() const { return dirty_; }
    void repaint() { if(visible_) dirty_=true; }
    void painted() { dirty_=false; }
    int soc() const { return soc_; }
    Status status() const { return status_; }
    const char* label() const {
        switch(status_) {
        case Status::Charging: return "CHG";
        case Status::Fast: return "FAST";
        case Status::Full: return "FULL";
        case Status::Error: return "ERR";
        default: return "WAIT";
        }
    }
    uint32_t color() const {
        return status_==Status::Fast || status_==Status::Full ? 0x00ff00u :
            status_==Status::Error ? 0xff4040u : status_==Status::Wait ? 0x888888u : 0x999999u;
    }
private:
    uint32_t updatedAt_=0;
    int16_t soc_=-1;
    Status status_=Status::Wait;
    bool visible_=false, dirty_=false;
};
