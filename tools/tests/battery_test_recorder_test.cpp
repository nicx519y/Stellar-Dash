#include "battery_test_recorder.hpp"
#include "firmware_metadata.h"
static_assert(FAULT_LOG_STORAGE_SIZE==65536, "fault retention unchanged");
static_assert(BATTERY_TEST_STORAGE_ADDR+BT_REGION_BYTES==CH585_FIRMWARE_STAGING_ADDR,"staging boundary");
static_assert(XORA_IMAGE_DATA_ADDR+XORA_IMAGE_DATA_BYTES==BATTERY_TEST_STORAGE_ADDR,"image boundary");
static_assert(USER_IMAGE_RESOURCES_ADDR+XORA_IMAGE_GUARD_BYTES==XORA_IMAGE_DATA_ADDR,"front guard");
#include <cassert>
#include <cstdio>
#include <cstring>
static uint8_t memory[65536], original[65536];
static int budget=-1, erased=0, readsFail=0;
static int rd(uint32_t o,void *p,uint32_t n) {
    assert(o+n<=sizeof(memory)); if(readsFail) return -1; memcpy(p,memory+o,n);return 0;
}
static int wr(uint32_t o,const void *p,uint32_t n) {
    assert(o+n<=sizeof(memory));
    for(uint32_t i=0;i<n;i++) { if(budget==0)return -1; if(budget>0)--budget; memory[o+i]&=((const uint8_t*)p)[i]; }
    return 0;
}
static int er(uint32_t) { ++erased; return -1; }
static fl_backend whole={rd,wr,er,sizeof(memory)};
static int br(uint32_t o,void *p,uint32_t n) { return rd(BT_HEADER_BYTES+o,p,n); }
static int bw(uint32_t o,const void *p,uint32_t n) { return wr(BT_HEADER_BYTES+o,p,n); }
static fl_backend battery={br,bw,er,BT_DATA_BYTES};
static void drain(BatteryTestRecorder &r) {
    while(r.count) { assert(fl_append(&battery,&r.queue[r.head])==FL_OK);r.pop(); }
}
int main() {
    // Reservation never erases or overwrites an existing tail, including unknown data.
    memset(memory,255,sizeof(memory)); memory[10000]=0x73;
    memcpy(original,memory,sizeof(memory)); assert(!bt_reserve_region(&whole));
    assert(!memcmp(original,memory,sizeof(memory)));
    // Every byte of a torn header remains uncommitted; no automatic erase.
    for(int cut=0;cut<20;cut++) {
        memset(memory,255,sizeof(memory));
        budget=cut;assert(!bt_reserve_region(&whole));
        assert(!bt_header_valid(reinterpret_cast<const bt_header *>(memory)));
    }
    budget=-1;memset(memory,255,sizeof(memory));assert(bt_reserve_region(&whole));
    assert(bt_header_valid(reinterpret_cast<const bt_header *>(memory))); assert(!bt_reserve_region(&whole));
    BatteryTestRecorder r;
    const uint32_t start=0xffff0000u;
    r.begin(start,3,0x010203,1);r.onBattery=true;
    uint32_t d[12]={}; r.push(BAT_TEST_START,start,d,12);drain(r);
    for(uint32_t t=1000;t<=BT_DURATION_MS;t+=1000) {
        r.advance(start+t);r.sample(t%7000!=0,3500,500);
        if(t%BT_SUMMARY_MS==0) {r.summary(start+t);drain(r);}
    }
    r.finish(start+BT_DURATION_MS,BT_TIMEOUT);drain(r);
    assert(r.elapsed==BT_DURATION_MS && r.discharge==BT_DURATION_MS);
    assert(r.summaries==288 && !r.incomplete);
    auto scan=fl_inspect(&battery);assert(scan.count==291 && scan.count<BT_SLOTS);
    fl_record latest;assert(fl_find(&battery,UINT32_MAX,0,0,0,&latest)==FL_OK);
    assert(latest.event==BAT_TEST_END && latest.data[2]==BT_TIMEOUT);
    assert(erased==0);
    // Queue and event quotas preserve a terminal record even under interrupt storms.
    r.begin(10,1,1,1);
    for(int i=0;i<100;i++)r.push(BAT_THRESHOLD_CHANGED,11,d,2);
    assert(r.count==12 && r.dropped==88);
    r.push(BAT_PROTECTION_ENTER,12,d,2,true);
    r.finish(20,BT_MANUAL);
    assert(!r.active && r.incomplete && r.count<=16);
    assert(r.queue[(r.head+r.count-1)%16].event==BAT_TEST_END);
    r.begin(0,1,1,1);
    for(int i=0;i<100;i++) {r.push(BAT_CONFIG_CHANGED,i,d,2);r.pop();}
    assert(r.events==52 && r.incomplete);
    assert(r.push(BAT_RECOVERY_ACTION,101,d,2,true));
    r.sleeping=true;r.advance(1000);assert(r.sleep==1000);
    r.sample(false,0,0);r.summary(1000);
    assert(r.queue[(r.head+r.count-1)%16].data[8]==0);

    // Charge summaries fit a 24h run in the same partition; packed units round-trip.
    r.begin(start,3,1,2);r.chargeMode=true;
    for(uint32_t t=1000;t<=BT_DURATION_MS;t+=1000) {
        r.advance(start+t);r.sample(true,3800,600);r.charging.accumulate(3800,9000,1500);
        if(t%BT_SUMMARY_MS==0) {
            r.summary(start+t);
            const auto &summary=r.queue[r.head];
            assert(summary.event==CHG_SAMPLE_SUMMARY && summary.words==14 && summary.data[0]==2);
            assert((summary.data[8]&65535)==3800 && (summary.data[8]>>16)==9000);
            assert(summary.data[9]==1500 && summary.data[10]==300);
            r.pop();
        }
    }
    r.finish(start+BT_DURATION_MS,BT_TIMEOUT);
    assert(r.summaries==288 && !r.incomplete && r.events+r.summaries+1<BT_SLOTS);
    assert(r.queue[r.head].event==CHG_TEST_END);
    r.begin(0,1,1,1);r.chargeMode=true;r.charging.inSegment=true;
    for(int i=0;i<100;i++)r.push(CHG_STATE_CHANGED,1,d,12);
    r.finish(2,BT_MANUAL);
    assert(!r.active && r.incomplete && r.count<=16);
    assert(r.queue[(r.head+r.count-1)%16].event==CHG_TEST_END);
    puts("battery recorder: 24h, wrap, atomic reservation, capacity, overflow and terminal priority passed");
}
