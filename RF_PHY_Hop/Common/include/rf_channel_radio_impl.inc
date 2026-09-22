#include "CONFIG.h"
#include "rf_channel_radio.h"
#include "rf_fast_debug.h"
#include "rf_link_clock.h"
static rfc_radio_ops_t ops;
static volatile uint8_t phase,channel,first,wake_valid,in_irq;
static uint32_t deadline,started,generation,phase_at,wake_at,armed_at;
static void arm_next(void){
    if(in_irq)return;
    TMR2_Disable();TMR2_ITCfg(DISABLE,TMR0_3_IT_CYC_END);TMR2_ClearITFlag(TMR0_3_IT_CYC_END);
    if(!phase && !wake_valid)return;
    uint32_t at=phase?phase_at:wake_at;
    if(wake_valid && (int32_t)(wake_at-at)<0)at=wake_at;
    uint32_t now=RF_LinkClockUs(),us=rfc_due(now,at)?2u:at-now;
    armed_at=at; /* Preserve the original deadline, including a late re-arm. */
    TMR2_TimerInit(us*(GetSysClock()/1000000u));TMR2_ITCfg(ENABLE,TMR0_3_IT_CYC_END);
}
void rfc_radio_init(const rfc_radio_ops_t *p){
    ops=*p;phase=first=wake_valid=in_irq=0;PFIC_SetPriority(TMR2_IRQn,0x80);PFIC_EnableIRQ(TMR2_IRQn);
}
void rfc_radio_cancel(void){phase=wake_valid=first=0;generation++;arm_next();}
void rfc_radio_wake(uint32_t at){
    if(!wake_valid || (int32_t)(at-wake_at)<0){wake_valid=1;wake_at=at;arm_next();}
}
void rfc_radio_schedule(uint8_t ch,uint32_t at){
    if(phase && channel==ch && deadline==at)return;
    generation++;channel=ch;deadline=at;
    uint32_t now=RF_LinkClockUs();
    phase_at=(int32_t)(at-now)>(int32_t)RFC_CALLBACK_DRAIN_US?at-RFC_CALLBACK_DRAIN_US:now;
    phase=1;arm_next();
}
uint8_t rfc_radio_pending(void){return phase>=2;}
uint8_t rfc_radio_first(void){return first;}
void rfc_radio_sent(void){first=0;}
static void service(uint32_t now){
    if(!phase || !rfc_due(now,phase_at))return;
    if(phase==1){phase=2;started=now;rff_log(RFF_EV_SWITCH,channel,now,deadline,0,generation);}
    if(phase==2){
        if(!ops.drained()){
            uint32_t limit=ops.fast()?RFF_READY_LIMIT_US:RFC_DRAIN_US;
            if(now-started<limit){phase_at=now+10u;return;}
            phase=0;ops.fault(RFF_PREPARE_OVERRUN,now);return;
        }
        if(!rfc_due(now,deadline)){phase_at=deadline;return;}
        uint32_t before=RF_LinkClockUs();uint8_t ok=ops.apply(channel);uint32_t after=RF_LinkClockUs();
        rff_log(RFF_EV_SDK,channel,after,before,after-before,generation);
        if(!ok){phase=0;ops.fault(RFF_RADIO,after);return;}
        phase=3;phase_at=after+RFC_SETTLE_US;return;
    }
    if(phase==3){
        if(ops.fast() && now-started>RFF_READY_LIMIT_US){phase=0;ops.fault(RFF_PREPARE_OVERRUN,now);return;}
        phase=0;first=1;rff_log(RFF_EV_READY,channel,now,deadline,now-started,generation);
        rff_maintenance(started,now);
        ops.ready(channel,1,generation);
    }
}
__INTERRUPT
__HIGH_CODE
void TMR2_IRQHandler(void){
    if(!TMR2_GetITFlag(TMR0_3_IT_CYC_END))return;
    TMR2_ClearITFlag(TMR0_3_IT_CYC_END);TMR2_Disable();
    uint32_t lock;SYS_DisableAllIrq(&lock);in_irq=1;
    uint32_t now=RF_LinkClockUs(),late=now-armed_at;
    if(rff_fault(RFF_FAULT_TIMER,channel,now))late=RFF_LATE_LIMIT_US+1u;
    if(late>g_rff_debug.max_late)g_rff_debug.max_late=late;
    if(ops.fast() && late>RFF_LATE_LIMIT_US){phase=wake_valid=0;ops.fault(RFF_LATE,now);}
    else {
        service(now);
        if(wake_valid && rfc_due(now,wake_at)){
            wake_valid=0;uint32_t next=ops.tick(RF_LinkClockUs());
            if(next){wake_valid=1;wake_at=next;}
        }
    }
    in_irq=0;arm_next();SYS_RecoverIrq(lock);
}
