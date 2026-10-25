#include <cassert>
#include <cstdint>
#include "battery_soc_estimator.hpp"
using E=BatterySocEstimator;
static E::Sample battery(uint32_t t,uint16_t mv=3200) {
    return {t,mv,550,0,true,false,false,false,false,false};
}
static E::Sample charge(uint32_t t,uint16_t ma=1550) {
    return {t,3661,550,ma,true,true,true,false,true,false};
}
int main() {
    E e; e.sample(battery(0));assert(e.soc()==0 && e.estimated());
    E coldBattery;coldBattery.sample(battery(0,3639));
    for(uint32_t t=1000;t<31000;t+=1000)coldBattery.sample(battery(t,3639));
    assert(coldBattery.estimated());
    coldBattery.sample(battery(31000,3639));assert(!coldBattery.estimated());
    e.sample(charge(1000));assert(e.soc()==0); // insertion voltage must not jump to 53%
    for(uint32_t t=2000;t<=3601000;t+=1000)e.sample(charge(t));
    assert(e.soc()==209); // 1550mAh / 7411mAh, remainder retained
    auto saved=e.soc();e.sample(charge(3601000));assert(e.soc()==saved);
    e.sample(charge(3602000,500)); // swap to weaker supply, no reset
    assert(e.soc()>=saved && e.soc()<=saved+1);
    e.sample(charge(3610000,500));assert(e.soc()<=saved+1 && e.estimated());
    auto invalid=charge(3611000);invalid.valid=false;e.sample(invalid);
    saved=e.soc();e.sample(charge(3612000,500));assert(e.soc()==saved);
    e.sample(battery(3613000,3200));e.sample(battery(3618000,3200));assert(e.soc()==saved);
    e.sample(battery(3623000,3200));assert(saved-e.soc()<=5);
    saved=e.soc();e.sample(battery(3628000,4100));assert(e.soc()==saved); // rebound
    e.anchorEmpty();assert(e.soc()==0);
    e.sample(charge(3629000));assert(e.soc()==0);
    E partial;partial.sample(battery(0,3639));assert(partial.soc()==500);
    partial.sample(charge(1000));assert(partial.soc()==500);
    for(uint32_t t=2000;t<=3601000;t+=1000)partial.sample(charge(t,500));
    assert(partial.soc()==567);
    E cold;cold.sample(charge(0));assert(cold.soc()==550 && cold.estimated());
    cold.sample(battery(1000,3639));
    for(uint32_t t=2000;t<=70000;t+=1000)cold.sample(battery(t,3639));
    assert(cold.soc()==500 && !cold.estimated());
    E full;auto f=charge(0);f.rawSoc=1000;full.sample(f);assert(full.soc()==990);
    f.charging=false;f.full=true;
    for(uint32_t t=1000;t<=30000;t+=1000){f.at=t;full.sample(f);assert(full.soc()==990);}
    f.at=31000;full.sample(f);assert(full.soc()==1000 && !full.estimated());
    full.sample(battery(32000,4139));
    for(uint32_t t=33000;t<=48000;t+=1000)full.sample(battery(t,4100));
    full.sample(charge(49000));assert(full.soc()==990);
    full.sample(charge(50000));assert(full.soc()==990); // old Full cannot restore 100%
    E high;high.sample(battery(0,4139));high.sample(charge(1000));
    assert(high.soc()==990); // 99% cap also applies to the insertion frame
    E gap;gap.sample(charge(0));f.at=1000;gap.sample(f);
    f.at=10000;gap.sample(f); // gap restarts the entire 30-second confirmation
    for(uint32_t t=11000;t<40000;t+=1000){f.at=t;gap.sample(f);assert(gap.soc()<1000);}
    f.at=40000;gap.sample(f);assert(gap.soc()==1000);
    E wrap;wrap.sample(battery(UINT32_MAX-2000));wrap.sample(charge(UINT32_MAX-1000));
    for(uint32_t i=1;i<=1800;i++)wrap.sample(charge(uint32_t(UINT32_MAX-1000+i*1000)));
    assert(wrap.soc()==104);
    E weak;weak.sample(battery(0,3639));weak.sample(charge(1000));
    auto w=charge(2000,0);w.weak=true;w.mv=3200;
    for(uint32_t t=2000;t<=11000;t+=1000){w.at=t;weak.sample(w);}
    assert(weak.soc()<500 && weak.estimated());
    E stopped;stopped.sample(battery(0));stopped.anchorEmpty();
    stopped.sample(battery(5000));stopped.sample(charge(6000));assert(stopped.soc()==0);
    E paused;paused.sample(battery(0));paused.sample(charge(1000));
    auto p=charge(2000);p.allowed=false;
    for(uint32_t t=2000;t<30000;t+=1000){p.at=t;paused.sample(p);}
    assert(paused.soc()==0 && paused.estimated());
}
