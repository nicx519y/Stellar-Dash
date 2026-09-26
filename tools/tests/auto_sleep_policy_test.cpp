#include "auto_sleep_policy.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

using State = AutoSleepPolicy::State;

int main()
{
    AutoSleepPolicy p;
    p.initialize(0, false);
    assert(!p.shouldPrepare(0, 10000, true));
    assert(!p.shouldPrepare(600000, 10000, true));
    p.initialize(0, true);
    assert(!p.shouldPrepare(0, 10000, true));
    assert(!p.shouldPrepare(10000, 10000, true));
    assert(p.shouldPrepare(30000, 10000, true));

    // Slow boots and return from exclusive modes start a NEW inactivity timer.
    p.initialize(0, true);
    assert(!p.shouldPrepare(60000, 10000, true));
    assert(!p.shouldPrepare(69999, 10000, true));
    assert(p.shouldPrepare(70000, 10000, true));
    assert(!p.shouldPrepare(71000, 10000, false));
    assert(!p.shouldPrepare(80000, 10000, true));
    assert(p.shouldPrepare(90000, 10000, true));

    // Held keys, releases and encoder activity postpone sleep.
    p.activity(95000);
    assert(!p.shouldPrepare(104999, 10000, true));
    assert(p.shouldPrepare(105000, 10000, true));
    for (uint32_t timeout : {10000u, 30000u, 60000u, 120000u, 300000u}) {
        p.initialize(0, true);
        (void)p.shouldPrepare(30000, timeout, true);
        assert(!p.shouldPrepare(30000 + timeout - 1, timeout, true));
        assert(p.shouldPrepare(30000 + timeout, timeout, true));
    }
    // Unsigned elapsed arithmetic survives the millisecond counter wrapping.
    p.initialize(0xffff0000u, true);
    (void)p.shouldPrepare(0xffff0000u, 60000u, true);
    assert(!p.shouldPrepare(0xffff0000u + 59999u, 60000u, true));
    assert(p.shouldPrepare(0xffff0000u + 60000u, 60000u, true));
    p.activity(0xfffffff0u);
    assert(!p.shouldPrepare(0x10u, 10000, true));
    assert(p.shouldPrepare(0xfffffff0u + 10000u, 10000, true));

    p.transition(State::Preparing, 1000);
    assert(!p.timedOut(1499)); assert(p.timedOut(1500));
    assert(!p.shouldPrepare(100000, 10000, true));
    p.transition(State::Sleeping, 2000);
    assert(!p.timedOut(100000));
    p.transition(State::RestoringLocal, 0xfffffff0u);
    assert(!p.timedOut(0xfffffff0u + 99u));
    assert(p.timedOut(0xfffffff0u + 100u));
    p.inhibit(); p.active(1000);
    assert(!p.shouldPrepare(600000, 10000, true));
    assert(!p.enabled());
    // A real initialization clears all previous sleep/failed runtime state.
    p.initialize(0, true);
    assert(p.state() == State::Active && p.enabled());

    SleepReleaseGate gate;
    gate.arm(1u << 18);
    assert(gate.filter((1u << 18) | 3u) == 0u);
    assert(gate.filter((1u << 18) | 3u) == 0u); // no long-press leak
    assert(gate.filter((1u << 18) | 2u | 4u) == 4u); // new key is usable
    assert(gate.filter(0u) == 0u);
    assert(!gate.pending());
    assert(gate.filter(1u << 18) == (1u << 18)); // second press is input
    gate.arm(0); assert(gate.pending());
    assert(gate.filter(0) == 0); assert(!gate.pending());

    SleepWakeKeys keys;
    keys.sample(1); keys.sample(0); keys.sample(1); keys.sample(0);
    assert(keys.takePressed() == 0); // bounce
    for (int i = 0; i < 5; ++i) keys.sample(1);
    assert(keys.stable() == 1 && keys.takePressed() == 1);
    for (int i = 0; i < 10; ++i) keys.sample(1);
    assert(keys.takePressed() == 0); // no repeated wake on hold
    for (int i = 0; i < 5; ++i) keys.sample(0);
    assert(keys.stable() == 0);
    for (int i = 0; i < 5; ++i) keys.sample(0x80040000u);
    for (int i = 0; i < 5; ++i) keys.sample(0);
    assert(keys.takePressed() == 0x80040000u); // short press retained for main
    std::puts("auto sleep policy, timeouts, recovery and wake filtering passed");
}
