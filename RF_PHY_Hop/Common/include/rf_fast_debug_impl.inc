#include "CONFIG.h"
#include "rf_fast_debug.h"
#include <string.h>
rff_debug_t g_rff_debug;
void rff_log(uint8_t kind,uint8_t channel,uint32_t at,uint32_t planned,uint32_t value,uint32_t generation){
    uint32_t lock;SYS_DisableAllIrq(&lock);uint8_t next=(g_rff_debug.head+1u)%RFF_LOG_COUNT;
    if(next==g_rff_debug.tail){g_rff_debug.overflow++;SYS_RecoverIrq(lock);return;}
    rff_event_t *e=&g_rff_debug.events[g_rff_debug.head];
    e->at=at;e->planned=planned;e->value=value;e->generation=generation;
    e->test=g_rff_debug.test;e->kind=kind;e->channel=channel;g_rff_debug.head=next;
    SYS_RecoverIrq(lock);
}
uint8_t rff_log_peek(rff_event_t *e){
    uint32_t lock;SYS_DisableAllIrq(&lock);uint8_t have=g_rff_debug.head!=g_rff_debug.tail;
    if(have)*e=g_rff_debug.events[g_rff_debug.tail];
    SYS_RecoverIrq(lock);return have;
}
void rff_log_commit(const rff_event_t *event){uint32_t lock;SYS_DisableAllIrq(&lock);
    if(g_rff_debug.tail!=g_rff_debug.head &&
       memcmp(&g_rff_debug.events[g_rff_debug.tail],event,sizeof(*event))==0)
        g_rff_debug.tail=(g_rff_debug.tail+1u)%RFF_LOG_COUNT;
    SYS_RecoverIrq(lock);}
void rff_debug_stop(uint32_t now){g_rff_debug.enabled=0;g_rff_debug.cleared_at=now;rff_log(RFF_EV_STOP,0,now,now,0,0);}
void rff_debug_start(uint16_t id,uint8_t kind,uint8_t channel,uint16_t count,uint16_t ms,uint32_t delay,uint32_t now){
    g_rff_debug.test=id;g_rff_debug.kind=kind;g_rff_debug.channel=channel;
    g_rff_debug.head=g_rff_debug.tail=0;
    g_rff_debug.count=count>255u?255u:count;g_rff_debug.begin=now+delay;
    g_rff_debug.end=g_rff_debug.begin+(ms>5000u?5000u:ms)*1000u;
    g_rff_debug.delay=delay;g_rff_debug.limited=count!=0;g_rff_debug.enabled=kind!=0 && ms;
    g_rff_debug.overflow=g_rff_debug.injected=0;
    g_rff_debug.need_fault_input=g_rff_debug.need_clear_input=0;
    rff_log(RFF_EV_INJECT,channel,now,g_rff_debug.begin,kind,0);
}
uint8_t rff_fault(uint8_t kind,uint8_t channel,uint32_t now){
    if(!g_rff_debug.enabled || (g_rff_debug.limited && !g_rff_debug.count))return 0;
    if((int32_t)(now-g_rff_debug.end)>=0){g_rff_debug.enabled=0;g_rff_debug.cleared_at=g_rff_debug.end;return 0;}
    if((int32_t)(now-g_rff_debug.begin)<0)return 0;
    if(g_rff_debug.kind==RFF_FAULT_ONLY_CHANNEL){
        if(kind!=RFF_FAULT_CHANNEL || channel==g_rff_debug.channel)return 0;
    } else if(kind!=g_rff_debug.kind || (g_rff_debug.channel!=255 && channel!=g_rff_debug.channel))return 0;
    if(g_rff_debug.limited)g_rff_debug.count--;
    g_rff_debug.injected++;g_rff_debug.last_injected=now;
    if(g_rff_debug.injected==1)g_rff_debug.need_fault_input=g_rff_debug.need_clear_input=1;
    if(g_rff_debug.limited && !g_rff_debug.count){g_rff_debug.enabled=0;g_rff_debug.cleared_at=now;}
    if(g_rff_debug.injected==1 || !g_rff_debug.enabled)
        rff_log(RFF_EV_FAULT,channel,now,g_rff_debug.begin,kind,0);
    return 1;
}
void rff_debug_input(uint8_t channel,uint32_t now,uint32_t generation){
    if(g_rff_debug.need_fault_input){g_rff_debug.need_fault_input=0;
        rff_log(RFF_EV_FAULT_INPUT,channel,now,g_rff_debug.begin,now-g_rff_debug.begin,generation);}
    if(g_rff_debug.need_clear_input && (!g_rff_debug.enabled || (int32_t)(now-g_rff_debug.end)>=0)){
        g_rff_debug.need_clear_input=0;
        uint32_t at=g_rff_debug.enabled?g_rff_debug.end:g_rff_debug.cleared_at;
        rff_log(RFF_EV_CLEAR_INPUT,channel,now,at,now-at,generation);
    }
}
void rff_maintenance(uint32_t begin,uint32_t end){
    if(g_rff_debug.maintenance_valid && (int32_t)(begin-g_rff_debug.maintenance_until)<0)
        begin=g_rff_debug.maintenance_until;
    if((int32_t)(end-begin)>0)g_rff_debug.maintenance_us+=end-begin;
    if(!g_rff_debug.maintenance_valid || (int32_t)(end-g_rff_debug.maintenance_until)>0)
        g_rff_debug.maintenance_until=end;
    g_rff_debug.maintenance_valid=1;
}
void rff_debug_gap(uint8_t phase,uint32_t gap,uint32_t now,uint32_t generation){
    if(phase>=5)return;
    g_rff_debug.gap_count[phase]++;
    if(gap>g_rff_debug.gap_max[phase])g_rff_debug.gap_max[phase]=gap;
    if(now-g_rff_debug.gap_at<1000000u)return;
    g_rff_debug.gap_at=now;
    for(uint8_t i=0;i<5;i++){
        if(g_rff_debug.gap_count[i])
            rff_log(RFF_EV_PHASE_GAP,i,now,g_rff_debug.gap_count[i],g_rff_debug.gap_max[i],generation);
        g_rff_debug.gap_count[i]=g_rff_debug.gap_max[i]=0;
    }
}
