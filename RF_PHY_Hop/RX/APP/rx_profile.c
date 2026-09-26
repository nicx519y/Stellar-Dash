#include "CONFIG.h"
#include "rx_profile.h"
#include <string.h>
static uint32_t hz_us, counts[RP_COUNT], timing[RT_TIMING_COUNT][RP_TIMING_WORDS];
static uint32_t spikes[RP_SPIKES][4], spike_next, last_snapshot, sequence, snapshot[RP_WORDS];
static uint32_t lock_start, irq_total, outer_start, state_generation, last_drop, drop_generation;
static uint32_t read_cost;
static uint8_t enabled, lock_depth, irq_depth, page, pending, speed, depth, water, pipeline;
/* Cycle thresholds live in RAM. No Flash loads or division in the recorder. */
static uint32_t bounds[8];
__HIGH_CODE void RXP_Init(uint32_t hz) {
    hz_us=hz/1000000u;if(!hz_us)hz_us=1;
    bounds[0]=8u*hz_us;bounds[1]=16u*hz_us;bounds[2]=32u*hz_us;bounds[3]=64u*hz_us;
    bounds[4]=125u*hz_us;bounds[5]=250u*hz_us;bounds[6]=500u*hz_us;bounds[7]=1000u*hz_us;
    read_cost=0xffffffffu;
    for(unsigned i=0;i<16;i++){uint32_t a=SysTick->CNT,b=SysTick->CNT;
        if(b-a<read_cost)read_cost=b-a;}
    last_snapshot=SysTick->CNT;
}
__HIGH_CODE void RXP_Enable(uint8_t on) {
    uint32_t key;SYS_DisableAllIrq(&key);
    if(on && !enabled)last_snapshot=SysTick->CNT;
    enabled=on && hz_us; if(!enabled)pending=0;
    SYS_RecoverIrq(key);
}
__HIGH_CODE void RXP_Add(unsigned n,uint32_t value) {
    if(!enabled || n>=RP_COUNT)return;
    uint32_t key;SYS_DisableAllIrq(&key);counts[n]+=value;SYS_RecoverIrq(key);
}
__HIGH_CODE void RXP_Count(unsigned n){RXP_Add(n,1);}
__HIGH_CODE void RXP_Drop(uint32_t id,uint32_t generation,uint8_t reset){
    if(!enabled)return;
    uint32_t key;SYS_DisableAllIrq(&key);uint32_t *s=spikes[spike_next++%RP_SPIKES];
    s[0]=id;s[1]=generation;s[2]=reset?0xfffffffeu:0xffffffffu;s[3]=SysTick->CNT;
    SYS_RecoverIrq(key);
}
/* Caller already holds the IRQ mask. Store maxima/spikes in cycles and only
 * convert the frozen, once-per-second export to microseconds. */
__HIGH_CODE static void record_time(unsigned n,uint32_t cycles,uint32_t id,uint32_t generation) {
    uint32_t *t=timing[n];
    t[0]++;if(cycles>t[1])t[1]=cycles;if(cycles>bounds[4])t[2]++;
    unsigned lo=0,hi=8;
    while(lo<hi){unsigned mid=(lo+hi)/2u;if(cycles<=bounds[mid])hi=mid;else lo=mid+1u;}
    t[3+lo]++;
    if(n==RT_READY && cycles>bounds[4]){uint32_t *s=spikes[spike_next++%RP_SPIKES];
        s[0]=id;s[1]=generation;s[2]=cycles;s[3]=SysTick->CNT;}
}
__HIGH_CODE void RXP_Time(unsigned n,uint32_t cycles,uint32_t id,uint32_t generation) {
    if(!enabled || n>=RT_TIMING_COUNT)return;
    uint32_t key;SYS_DisableAllIrq(&key);
    record_time(n,cycles,id,generation);SYS_RecoverIrq(key);
}
/* CPU values exclude nested instrumented RF/USB/timer scopes, not SDK time
 * preceding the RF callback or other uninstrumented IRQs. Never call them WCET. */
/* Writing the caller's scope avoids the compiler-generated Flash memcpy
 * from returning a struct, previously executed before RF rearm on every IRQ. */
__HIGH_CODE void RXP_Begin(rxp_scope_t *s,unsigned metric,uint8_t irq) {
    s->active=0;if(!enabled)return;
    uint32_t key;SYS_DisableAllIrq(&key);s->start=SysTick->CNT;
    s->metric=metric;s->irq=irq;s->active=1;s->nested=irq_total;
    if(irq && !irq_depth++)outer_start=s->start;
    SYS_RecoverIrq(key);
}
__HIGH_CODE void RXP_End(rxp_scope_t *s) {
    if(!s->active)return;
    uint32_t key;SYS_DisableAllIrq(&key);uint32_t now=SysTick->CNT,elapsed=now-s->start;
    uint32_t children=irq_total-s->nested;
    if(s->irq){
        /* Each completed inner scope contributes to its enclosing scope. */
        if(irq_depth)--irq_depth;
        if(!irq_depth)irq_total=s->nested+(now-outer_start);
        else irq_total+=elapsed-children;
    }
    if(enabled)record_time(s->metric,elapsed,0,0);
    unsigned cpu=s->metric==RT_RF?RT_RF_CPU:s->metric==RT_USB?RT_USB_CPU:
                 s->metric==RT_BACKGROUND?RT_BACKGROUND_CPU:RT_TIMING_COUNT;
    if(enabled && cpu<RT_TIMING_COUNT)record_time(cpu,elapsed>=children?elapsed-children:0,0,0);
    SYS_RecoverIrq(key);
}
__HIGH_CODE void RXP_Lock(uint32_t *saved){
    SYS_DisableAllIrq(saved);
    if(!lock_depth++)lock_start=SysTick->CNT;
}
__HIGH_CODE void RXP_Unlock(uint32_t saved){
    if(lock_depth && !--lock_depth && enabled)record_time(RT_CRITICAL,SysTick->CNT-lock_start,0,0);
    SYS_RecoverIrq(saved);
}
__HIGH_CODE void RXP_State(uint8_t s,uint8_t d,uint8_t w,uint8_t p,uint32_t gen,uint32_t drop,uint32_t dg){
    uint32_t key;SYS_DisableAllIrq(&key);
    speed=s;depth=d;water=w;pipeline=p;state_generation=gen;last_drop=drop;drop_generation=dg;
    SYS_RecoverIrq(key);
}
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
__HIGH_CODE int RXP_Page(uint8_t report[32]){
    if(!enabled)return 0;
    uint32_t now=SysTick->CNT;
    if(!pending){
        if(now-last_snapshot<hz_us*1000000u)return 0;
        /* Freeze one bounded RAM snapshot. Export is preemptible and low-rate. */
        uint32_t key;SYS_DisableAllIrq(&key);uint32_t snapshot_start=SysTick->CNT;
        snapshot[0]=now;snapshot[1]=now-last_snapshot;snapshot[2]=speed;
        snapshot[3]=depth;snapshot[4]=water;snapshot[5]=pipeline;
        snapshot[6]=state_generation;snapshot[7]=last_drop;snapshot[8]=drop_generation;
        snapshot[9]=read_cost;snapshot[10]=hz_us;snapshot[11]=spike_next;
        /* Volatile word copies avoid a slow Flash libc call under the mask. */
        volatile uint32_t *out=snapshot+RP_HEADER_WORDS;
        for(unsigned i=0;i<RP_COUNT;i++)*out++=counts[i];
        for(unsigned i=0;i<RT_TIMING_COUNT;i++)for(unsigned j=0;j<RP_TIMING_WORDS;j++)*out++=timing[i][j];
        for(unsigned i=0;i<RP_SPIKES;i++)for(unsigned j=0;j<4;j++)*out++=spikes[i][j];
        last_snapshot=now;sequence++;page=0;pending=1;
        record_time(RT_CRITICAL,SysTick->CNT-snapshot_start,0,0);
        SYS_RecoverIrq(key);
        /* Conversion is background work, with RF/USB interrupts enabled. */
        snapshot[1]/=hz_us;
        for(unsigned i=0;i<RT_TIMING_COUNT;i++){
            uint32_t *v=&snapshot[RP_HEADER_WORDS+RP_COUNT+i*RP_TIMING_WORDS+1u];
            *v=*v/hz_us+(*v%hz_us!=0u);
        }
        for(unsigned i=0;i<RP_SPIKES;i++){
            uint32_t *v=&snapshot[RP_HEADER_WORDS+RP_COUNT+RT_TIMING_COUNT*RP_TIMING_WORDS+i*4u+2u];
            if(*v<0xfffffffeu)*v=*v/hz_us+(*v%hz_us!=0u);
        }
    }
    memset(report,0,32);put(report,0x31505852u); /* RXP1 */
    report[4]=(uint8_t)sequence;report[5]=(uint8_t)(sequence>>8);report[6]=page;report[7]=1;
    for(unsigned i=0;i<6;i++){unsigned word=page*6u+i;if(word<RP_WORDS)put(report+8+i*4,snapshot[word]);}
    return 1;
}
void RXP_PageSent(void){if(pending && ++page>=(RP_WORDS+5u)/6u)pending=0;}
