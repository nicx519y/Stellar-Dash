#include <cassert>
#include <cstdio>
#include "battery_policy.hpp"
#include "battery_access_policy.hpp"
#include "screen_control/battery_notice_policy.hpp"

int main() {
    assert(batteryRejectsCommand("create_firmware_upgrade_session",true,false,false));
    assert(batteryRejectsCommand("begin_release_install",true,true,false));
    assert(batteryRejectsCommand("update_profile",true,true,false));
    assert(!batteryRejectsCommand("update_profile",true,false,false));
    assert(!batteryRejectsCommand("upload_firmware_chunk",true,true,false));
    assert(!batteryRejectsCommand("complete_firmware_upgrade_session",true,true,false));
    assert(!batteryRejectsCommand("abort_release_install",true,true,false));
    assert(!batteryRejectsCommand("get_config",true,true,true));
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
    for (unsigned t=42000;t<45000;++t) p.sample(t,true,3200,80,false);
    assert(p.level()!=BatteryLevel::Critical);
    p.sample(45000,true,3200,80,false);
    assert(p.level()==BatteryLevel::Critical);
    for (unsigned t=46000;t<=80000;t+=1000) p.sample(t,true,3800,500,false);
    assert(p.level()==BatteryLevel::Critical); // rebound cannot unlock
    assert(!p.acknowledgeRecovery());
    // Qualified power permits recovery on the first sample, even below 3.2 V.
    p.sample(81000,true,3100,40,true);
    assert(p.level()==BatteryLevel::RecoveryReady);
    p.sample(82000,false,3100,40,true); assert(p.level()==BatteryLevel::Critical);
    assert(!p.acknowledgeRecovery());
    p.sample(83000,true,3100,40,false); // cable lost / charging blocked
    assert(p.level()==BatteryLevel::Critical);
    p.sample(84000,true,3100,40,true);
    assert(p.acknowledgeRecovery() && p.level()!=BatteryLevel::Critical);
    for(unsigned t=85000;t<=103000;t+=1000) {
        p.sample(t,true,3100,40,true);
        assert(p.level()!=BatteryLevel::Critical); // no protection loop on external power
    }
    assert(p.level()==BatteryLevel::Low); // the warning still confirms normally
    for(unsigned t=104000;t<=107000;t+=1000) p.sample(t,true,3100,40,false);
    assert(p.level()==BatteryLevel::Critical); // unplugged and depleted again after recovery
    // A cold start on qualified power must not enter protection either.
    BatteryPolicy poweredStart;
    for(unsigned t=0;t<=12000;t+=1000) {
        poweredStart.sample(t,true,3000,20,true);
        assert(poweredStart.level()!=BatteryLevel::Critical);
    }
    // A short dropout does not accumulate with the next undervoltage interval.
    poweredStart.sample(13000,true,3000,20,false);
    poweredStart.sample(15000,true,3000,20,false);
    poweredStart.sample(16000,true,3000,20,true);
    poweredStart.sample(17000,true,3000,20,false);
    poweredStart.sample(19000,true,3000,20,false);
    assert(poweredStart.level()==BatteryLevel::Low);
    poweredStart.sample(20000,true,3000,20,false);
    assert(poweredStart.level()==BatteryLevel::Critical);
    BatteryPolicy jitter;
    for(unsigned t=0;t<=12000;t+=1000) {
        jitter.sample(t,true,(t/1000)%2 ? 3201 : 3200,200,false);
        assert(jitter.level()!=BatteryLevel::Critical);
    }
    BatteryPolicy voltageOnly;
    for(unsigned t=0;t<=10000;t+=1000) voltageOnly.sample(t,true,3450,101,false);
    assert(voltageOnly.level()==BatteryLevel::Normal); // voltage entry is strictly below
    for(unsigned t=11000;t<=21000;t+=1000) voltageOnly.sample(t,true,3449,101,false);
    assert(voltageOnly.level()==BatteryLevel::Normal); // calibrated SOC controls warning
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
