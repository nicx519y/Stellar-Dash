#include "stop_timer_timing.hpp"
#include "power_manager.hpp"
#include <cassert>
#include <initializer_list>

int main() {
    using namespace StopTimerTiming;
    assert(countsForMs(32000, 1) == 8);
    assert(countsForMs(32000, 10) == 80);
    assert(countsForMs(32000, 50) == 400); // clock recovery timeout stays 50 ms
    assert(countsForMs(32000, 1000) == 8000);
    assert(countsForMs(32000, 5000) == 40000);
    assert(countsForMs(32001, 1) == 9); // round up, never zero/early compare
    uint32_t remainder = 0;
    assert(elapsedMs(10, 40010, 32000, remainder) == 5000 && remainder == 0);
    assert(elapsedMs(10, 90, 32000, remainder) == 10); // early key wake
    assert(elapsedMs(65530, 2, 32000, remainder) == 1); // counter wrap
    unsigned total = 0;
    for (unsigned i = 0; i < 8; ++i) total += elapsedMs(0, 1, 32000, remainder);
    assert(total == 1 && remainder == 0); // fractional time survives short wakes
    uint32_t tick = 0xfffffff0u;
    tick += elapsedMs(0, 40000, 32000, remainder);
    assert(tick == 4984u); // HAL millisecond wrap

    PowerSnapshot state;
    assert(state.sleepMaintenanceIntervalMs() == 1000); // unknown is conservative
    state.valid = state.charger_online = state.gauge_online = true;
    state.fault_bits = 0;
    state.cell_mv = 3800;
    state.charge_state = PowerChargeState::Discharging;
    assert(state.sleepMaintenanceIntervalMs() == 5000);
    const auto battery = state;
    state.vbus_present = true;
    assert(state.sleepMaintenanceIntervalMs() == 1000);
    state = battery; state.vbus_mv = 5000; // input detected before PG confirms it
    assert(state.sleepMaintenanceIntervalMs() == 1000);
    state = battery; state.cell_mv = 3450;
    assert(state.sleepMaintenanceIntervalMs() == 1000);
    state = battery; state.cell_mv = 3451;
    assert(state.sleepMaintenanceIntervalMs() == 5000);
    for (const auto charge : {PowerChargeState::Charging, PowerChargeState::Full,
                             PowerChargeState::Fault, PowerChargeState::Unknown}) {
        state = battery; state.charge_state = charge;
        assert(state.sleepMaintenanceIntervalMs() == 1000);
    }
    state = battery; state.valid = false;
    assert(state.sleepMaintenanceIntervalMs() == 1000);
    state = battery; state.gauge_online = false;
    assert(state.sleepMaintenanceIntervalMs() == 1000);
    state = battery; state.charger_online = false;
    assert(state.sleepMaintenanceIntervalMs() == 1000);
    for (unsigned bit = 0; bit < 12; ++bit) {
        state = battery; state.fault_bits = 1u << bit;
        assert(state.sleepMaintenanceIntervalMs() == 1000);
    }
}
