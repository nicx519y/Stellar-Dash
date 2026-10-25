#include <cassert>
#include <cstdio>
#include "power_load_policy.hpp"

int main() {
    // Normal operation is outside supply monitoring, even near full, at low
    // SOC without protection, during taper/thermal regulation, or with a dip.
    for (unsigned scenario=0; scenario<4; ++scenario) {
        PowerLoadPolicy normal;
        for (unsigned t=0;t<=60000;t+=1000) {
            const uint16_t mv=scenario==3 ? uint16_t(3800-t/200) : 4190;
            normal.sample(t,true,true,true,false,mv,scenario==3?80:980,
                          scenario==0?50:0,scenario!=0,scenario==2,scenario==1);
            assert(!normal.insufficient() && !normal.locked());
            assert(normal.ledPercent()==100 && normal.lcdPercent()==100);
        }
        normal.sample(64000,false,true,false,false,0,0,0,true,true,false);
        assert(!normal.insufficient() && normal.ledPercent()==100);
    }

    PowerLoadPolicy p;
    p.sample(0,true,true,true,true,3100,40,300,false,false,false);
    assert(p.ledPercent()==0 && p.lcdPercent()==10 && p.recoveryAllowed(3100));
    // Recovery does not imply permission for a new upgrade or alter SOC.
    for(unsigned t=1000;t<=30000;t+=1000) {
        p.sample(t,true,true,true,false,3150,50,300,false,false,false);
        assert(p.ledPercent()==0);
    }
    p.sample(31000,true,true,true,false,3150,50,300,false,false,false);
    assert(p.ledPercent()==25);
    for(unsigned t=32000;t<=64000;t+=1000)
        p.sample(t,true,true,true,false,3200,60,300,false,false,false);
    assert(p.ledPercent()==100);
    // DPM alone while the battery is still charging must not shed the lights.
    for(unsigned t=65000;t<=80000;t+=1000)
        p.sample(t,true,true,true,false,3300,80,300,true,false,false);
    assert(p.ledPercent()==100 && !p.insufficient());
    // Once current dries up, shed before the battery reaches its floor.
    for(unsigned t=81000;t<=84000;t+=1000)
        p.sample(t,true,true,true,false,3300,80,0,true,false,false);
    assert(p.ledPercent()==0 && p.insufficient() && !p.locked());
    // A second declining window under the reduced load locks protection.
    for(unsigned t=85000;t<=95000;t+=1000)
        p.sample(t,true,true,true,false,uint16_t(3300-(t-85000)/200),70,0,true,false,false);
    assert(p.locked() && !p.recoveryAllowed(3800));
    for(unsigned t=96000;t<=130000;t+=1000)
        p.sample(t,true,true,true,true,3500,140,300,false,false,false);
    assert(p.locked()); // rebound/charging without reserve is not another trial
    for(unsigned t=131000;t<161000;t+=1000)
        p.sample(t,true,true,true,true,3550,150,300,false,false,false);
    assert(p.locked());
    p.sample(161000,true,true,true,true,3550,150,300,false,false,false);
    assert(!p.locked() && p.ledPercent()==0 && p.recoveryAllowed(3550));

    PowerLoadPolicy deep;
    deep.sample(0,true,true,true,false,2990,10,400,false,false,false);
    deep.sample(1000,true,true,true,false,2990,10,400,false,false,false);
    assert(deep.locked()); // independent of charge enable and even positive ICHGR
    deep.sample(2000,true,true,true,false,2990,10,400,false,false,false);
    assert(!deep.recoveryAllowed(3050));
    deep.sample(3000,true,false,false,true,3300,40,0,false,false,false);
    assert(deep.locked());
    deep.sample(4000,true,false,false,true,3300,40,0,false,false,false);
    assert(!deep.locked()); // deliberate disconnect starts a new attempt

    PowerLoadPolicy weak;
    weak.sample(0,true,true,true,true,3100,20,0,true,false,false);
    for(unsigned t=0;t<=6000;t+=1000)
        weak.sample(t,true,true,true,false,3100,20,0,true,false,false);
    assert(weak.locked()); // flat low voltage + exhausted source cannot run forever

    PowerLoadPolicy stale;
    stale.sample(0,true,true,true,true,3100,20,300,false,false,false);
    for(unsigned t=1000;t<=4000;t+=1000)
        stale.sample(t,false,true,true,false,3100,20,300,false,false,false);
    assert(stale.locked());
    BatteryPolicy battery;
    battery.sample(0,false,0,0,false,true);
    assert(battery.level()==BatteryLevel::Critical && !battery.acknowledgeRecovery());

    // Tick wrap and data gaps cannot manufacture the healthy interval.
    PowerLoadPolicy wrap;
    const uint32_t start=UINT32_MAX-15000u;
    wrap.sample(start,true,true,true,true,3300,50,300,false,false,false);
    for(unsigned t=0;t<30000;t+=1000) {
        wrap.sample(start+t,true,true,true,false,3300,50,300,false,false,false);
        assert(wrap.ledPercent()==0);
    }
    wrap.sample(start+30000,true,true,true,false,3300,50,300,false,false,false);
    assert(wrap.ledPercent()==25);
    wrap.sample(start+34000,true,true,true,false,3300,50,300,false,false,false);
    assert(wrap.ledPercent()==0);
    // Charge termination is not an empty source: a healthy full battery can restore.
    for(unsigned t=35000;t<=100000;t+=1000)
        wrap.sample(start+t,true,true,true,false,4190,1000,0,false,false,true);
    assert(wrap.ledPercent()==100);
    // Once the recovered session ends, later DPM/thermal flags cannot relimit.
    for(unsigned t=101000;t<=130000;t+=1000)
        wrap.sample(start+t,true,true,true,false,4190,1000,0,true,true,true);
    assert(!wrap.insufficient() && !wrap.locked() && wrap.ledPercent()==100);

    // Reserve must be sustained while awake. A gap, invalid reading or a dip
    // below the exit threshold restarts the interval; protected mode cannot
    // silently unlock itself just because SOC is now high.
    PowerLoadPolicy finish;
    for(unsigned t=0;t<=40000;t+=1000)
        finish.sample(t,true,true,true,true,4190,980,0,true,false,false);
    assert(finish.ledPercent()==0);
    for(unsigned t=41000;t<=60000;t+=1000)
        finish.sample(t,true,true,true,false,3600,150,0,true,false,false);
    finish.sample(61000,false,true,true,false,3600,150,0,true,false,false);
    for(unsigned t=62000;t<=82000;t+=1000)
        finish.sample(t,true,true,true,false,3600,150,0,true,false,false);
    finish.sample(86000,true,true,true,false,3600,150,0,true,false,false);
    for(unsigned t=87000;t<=106000;t+=1000)
        finish.sample(t,true,true,true,false,3600,150,0,true,false,false);
    finish.sample(107000,true,true,true,false,3600,149,0,true,false,false);
    for(unsigned t=108000;t<138000;t+=1000) {
        finish.sample(t,true,true,true,false,3600,150,0,true,false,false);
        assert(finish.ledPercent()==0);
    }
    finish.sample(138000,true,true,true,false,3600,150,0,true,false,false);
    assert(finish.ledPercent()==100 && !finish.insufficient() && !finish.locked());
    // A later genuine protection episode starts a fresh monitoring session.
    finish.sample(139000,true,true,true,true,3100,20,0,true,false,false);
    for(unsigned t=140000;t<=146000;t+=1000)
        finish.sample(t,true,true,true,false,3100,20,0,true,false,false);
    assert(finish.locked() && finish.insufficient() && finish.ledPercent()==0);
    puts("power load policy: normal bypass, recovery session/exit, ramp, DPM, decline, floor, retry latch, stale data and wrap passed");
}
