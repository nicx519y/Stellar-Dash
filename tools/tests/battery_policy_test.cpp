#include <cassert>
#include <cstdio>
#include "battery_policy.hpp"
#include "screen_control/battery_notice_policy.hpp"

int main() {
    BatteryPolicy p;
    for (unsigned t=0;t<10000;t+=1000) {
        p.sample(t,true,3600,100,false);
        assert(p.level()==BatteryLevel::Normal);
    }
    p.sample(10000,true,3600,100,false);
    assert(p.level()==BatteryLevel::Low);
    for (unsigned t=11000;t<41000;t+=1000) p.sample(t,true,3550,150,false);
    assert(p.level()==BatteryLevel::Low);
    p.sample(41000,true,3550,150,false); assert(p.level()==BatteryLevel::Normal);
    // Dense IRQ samples never substitute for elapsed milliseconds.
    for (unsigned t=42000;t<45000;++t) p.sample(t,true,3200,80,true);
    assert(p.level()!=BatteryLevel::Critical);
    p.sample(45000,true,3200,80,true);
    assert(p.level()==BatteryLevel::Critical); // also while plugged in
    for (unsigned t=46000;t<=80000;t+=1000) p.sample(t,true,3800,500,false);
    assert(p.level()==BatteryLevel::Critical); // rebound cannot unlock
    assert(!p.acknowledgeRecovery());
    for(unsigned t=81000;t<91000;t+=1000) p.sample(t,true,3550,100,true);
    assert(p.level()==BatteryLevel::Critical);
    p.sample(91000,true,3550,100,true); assert(p.level()==BatteryLevel::RecoveryReady);
    p.sample(92000,false,3550,100,true); assert(p.level()==BatteryLevel::Critical);
    for(unsigned t=93000;t<=103000;t+=1000) p.sample(t,true,3550,100,true);
    assert(p.acknowledgeRecovery() && p.level()!=BatteryLevel::Critical);
    // Gap/invalid data restart confirmation, including around tick wrap.
    p = {};
    p.sample(0,true,3100,40,false);
    p.sample(5000,true,3100,40,false);
    p.sample(7000,false,3100,40,false);
    assert(p.level()==BatteryLevel::Normal);
    p = {};
    const uint32_t start=UINT32_MAX-1500u;
    for(unsigned i=0;i<=3;++i) p.sample(start+i*1000,true,3200,40,false);
    assert(p.level()==BatteryLevel::Critical);
    BatteryRecoveryButton button;
    assert(!button.update(false,true));
    assert(!button.update(true,true));
    assert(!button.update(true,false)); // held through readiness is discarded
    assert(!button.update(true,true));
    assert(!button.update(false,true)); // cable removed midway
    assert(!button.update(true,false));
    assert(!button.update(true,true));
    assert(button.update(true,false));

    BatteryNoticePolicy ui;
    ui.update(start,true); assert(ui.visible());
    ui.update(start+5000,true); assert(!ui.visible());
    ui.update(start+6000,true); assert(!ui.visible());
    ui.update(start+7000,false);
    ui.update(start+8000,true); assert(ui.visible());
    ui.protect(start+9000,true);
    assert(ui.notice()==BatteryNoticePolicy::Notice::Protecting);
    ui.update(start+11000,true); assert(!ui.visible() && ui.protectedMode());
    ui.inspect(start+12000); assert(ui.visible());
    ui.update(start+15000,true); assert(!ui.visible());
    ui.protect(start+16000,false); assert(!ui.protectedMode());
    puts("battery policies: thresholds, hysteresis, time/gaps/wrap, recovery button, notices passed");
}
