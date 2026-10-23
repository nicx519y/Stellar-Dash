#pragma once
#include "battery_test_log.h"

// v2 charge snapshot (12 words after version/session): segment, detail,
// cell mV, SOC permille, input mV, charge mA, state|input<<8|valid<<9,
// charge block, faults, sampled uptime, cumulative input ms, charging ms.
struct ChargeTestRecorder {
    uint32_t segment=0, inputMs=0, chargingMs=0, lastSample=0, fullAt=0;
    uint32_t cellSum=0, inputSum=0, currentSum=0;
    uint16_t inputMin=65535, inputMax=0, currentMin=65535, currentMax=0;
    uint16_t lastSoc=0, lastCell=0, lastInput=0, lastCurrent=0;
    uint32_t lastState=0, lastBlock=0, lastFault=0, milestoneMask=0;
    bool inSegment=false, previousValid=false, previousInput=false, previousCharging=false;
    bool fullPending=false, midCharge=false;

    template<class R> void snapshot(R &r,uint16_t event,uint32_t now,uint32_t detail,bool important=false) {
        const uint32_t d[]={segment,detail,lastCell,lastSoc,lastInput,lastCurrent,
            lastState|(uint32_t(previousInput)<<8)|(uint32_t(previousValid)<<9),
            lastBlock,lastFault,lastSample,inputMs,chargingMs};
        r.push(event,now,d,12,important);
    }
    template<class R> void close(R &r,uint32_t now,uint32_t reason) {
        if(!inSegment) return;
        snapshot(r,CHG_SEGMENT_END,now,reason,true); inSegment=false; fullPending=false;
    }
    void accumulate(uint16_t cell,uint16_t input,uint16_t current) {
        cellSum+=cell; inputSum+=input; currentSum+=current;
        if(input<inputMin) inputMin=input;
        if(input>inputMax) inputMax=input;
        if(current<currentMin) currentMin=current;
        if(current>currentMax) currentMax=current;
    }
    static uint32_t pair(uint32_t lo,uint32_t hi) { return (lo&65535u)|((hi&65535u)<<16); }
    template<class R> void summary(R &r,uint32_t now) {
        const uint32_t n=r.validCount;
        // Fixed 48-byte payload: range pairs, averages, counts and totals.
        const uint32_t d[]={r.bucketAt,now,pair(n?r.minMv:0,r.maxMv),
            pair(n?inputMin:0,inputMax),pair(n?currentMin:0,currentMax),pair(r.firstSoc,r.lastSoc),
            pair(n?cellSum/n:0,n?inputSum/n:0),n?currentSum/n:0,n,r.missing,inputMs,chargingMs};
        r.push(CHG_SAMPLE_SUMMARY,now,d,12);
        cellSum=inputSum=currentSum=0; inputMin=currentMin=65535; inputMax=currentMax=0;
    }
};
