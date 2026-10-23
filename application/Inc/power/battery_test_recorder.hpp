#pragma once
#include "battery_test_log.h"
#include "charge_test_recorder.hpp"

// Portable producer policy. No heap, hardware, Flash writes or static clocks.
struct BatteryTestRecorder {
    ChargeTestRecorder charging{};
    bool chargeMode=false;
    fl_record queue[16]{};
    uint32_t boot=0, firmware=0, session=0, started=0, last=0, elapsed=0;
    uint32_t discharge=0, sleep=0, stopCount=0, stopFailed=0, stopMs=0, stopGates=0, writeMs=0, dropped=0;
    uint32_t ordinaryStops=0, ordinaryStopMs=0, protectionStops=0, protectionStopMs=0;
    uint32_t ordinaryFailures=0, protectionFailures=0;
    uint32_t bucketAt=0, validCount=0, missing=0, firstSoc=0, lastSoc=0, minMv=65535, maxMv=0;
    uint32_t summaries=0, events=0;
    uint8_t head=0, count=0;
    bool active=false, discharged=false, onBattery=false, sleeping=false, incomplete=false;

    bool push(uint16_t event, uint32_t now, const uint32_t *data, uint8_t words, bool important=false) {
        const bool terminal=event==BAT_TEST_END || event==CHG_TEST_END;
        const bool trend=event==BAT_SAMPLE_SUMMARY || event==CHG_SAMPLE_SUMMARY;
        if (!active && !terminal) return false;
        // 290 trend slots, 60 events (last 8 for protection/recovery), 2 terminal slots.
        if (words>12) { ++dropped; incomplete=true; return false; }
        // Leave queue room for protection and the terminal summary/end pair.
        if ((trend && summaries>=290) ||
            (!trend && !terminal && events>=(important?60u:52u)) ||
            count>=(terminal?16u:important?14u:12u)) { ++dropped; incomplete=true; return false; }
        auto &r=queue[(head+count)%16]; r={};
        r.magic=FL_MAGIC; r.boot=boot; r.firmware=firmware; r.uptime_ms=now;
        r.event=event; r.source=BT_SOURCE; r.category=BT_CATEGORY; r.severity=BT_INFO;
        r.repeat=1; r.flags=FL_PENDING; r.words=uint8_t(words+2);
        r.data[0]=chargeMode?CT_VERSION:BT_VERSION; r.data[1]=session;
        for (uint8_t i=0;i<words && i<12;i++) r.data[i+2]=data[i];
        ++count;
        if(trend) ++summaries;
        else if(!terminal) ++events;
        return true;
    }
    void begin(uint32_t now, uint32_t b, uint32_t fw, uint32_t id) {
        *this={}; active=true; started=last=bucketAt=now; boot=b; firmware=fw; session=id;
    }
    void advance(uint32_t now) {
        if(!active) return;
        const uint32_t dt=now-last; last=now; elapsed+=dt;
        if(onBattery) discharge+=dt;
        if(sleeping) sleep+=dt;
    }
    void sample(bool valid, uint16_t mv, uint16_t soc) {
        if(!active) return;
        if(!valid) { ++missing; return; }
        if(!validCount) firstSoc=soc;
        ++validCount; lastSoc=soc;
        if(mv<minMv) minMv=mv;
        if(mv>maxMv) maxMv=mv;
    }
    void summary(uint32_t now) {
        if(!validCount && !missing) { bucketAt=now; return; }
        const uint32_t data[]={bucketAt,now,validCount?minMv:0u,maxMv,
            firstSoc,lastSoc,validCount,missing,discharge,sleep,writeMs,dropped};
        if(chargeMode) charging.summary(*this,now);
        else push(BAT_SAMPLE_SUMMARY,now,data,12);
        bucketAt=now; validCount=missing=firstSoc=lastSoc=maxMv=0; minMv=65535;
    }
    void finish(uint32_t now, uint32_t reason) {
        if(!active) return;
        advance(now); summary(now);
        if(chargeMode) {
            charging.close(*this,now,reason);
            const uint32_t d[]={reason,elapsed,charging.inputMs,charging.chargingMs,writeMs,dropped,incomplete?1u:0u};
            push(CHG_TEST_END,now,d,7,true); active=false; return;
        }
        const uint32_t stats[]={sleep,stopCount,stopFailed,stopMs,writeMs,stopGates,
            ordinaryStops,ordinaryStopMs,protectionStops,protectionStopMs,ordinaryFailures,protectionFailures};
        push(BAT_SLEEP_SUMMARY,now,stats,12,true);
        const uint32_t data[]={reason,elapsed,discharge,sleep,writeMs,dropped,incomplete?1u:0u};
        push(BAT_TEST_END,now,data,7,true);
        active=false;
    }
    void pop() { if(count) { head=(head+1)%16; --count; } }
};
