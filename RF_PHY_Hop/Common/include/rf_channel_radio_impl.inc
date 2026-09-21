#include "CONFIG.h"
#include "rf_channel_radio.h"
#include "rf_link_clock.h"
static rfc_radio_ops_t ops;
static volatile uint8_t phase,channel,first;
static uint32_t deadline,started,generation;
static void arm(uint32_t us) {
    TMR2_Disable();TMR2_ITCfg(DISABLE,TMR0_3_IT_CYC_END);TMR2_ClearITFlag(TMR0_3_IT_CYC_END);
    if(us<2u)us=2u;
    TMR2_TimerInit(us*(GetSysClock()/1000000u));TMR2_ITCfg(ENABLE,TMR0_3_IT_CYC_END);
}
void rfc_radio_init(const rfc_radio_ops_t *p) {
    ops=*p;phase=first=0;PFIC_SetPriority(TMR2_IRQn,0x80);PFIC_EnableIRQ(TMR2_IRQn);
}
void rfc_radio_cancel(void) {
    TMR2_Disable();TMR2_ITCfg(DISABLE,TMR0_3_IT_CYC_END);TMR2_ClearITFlag(TMR0_3_IT_CYC_END);
    phase=0;generation++;
}
void rfc_radio_schedule(uint8_t ch,uint32_t at) {
    if(phase && channel==ch && deadline==at)return;
    rfc_radio_cancel();channel=ch;deadline=at;phase=1;
    uint32_t now=RF_LinkClockUs();arm(rfc_due(now,at)?2u:at-now);
}
uint8_t rfc_radio_pending(void){return phase>=2;}
uint8_t rfc_radio_first(void){return first;}
void rfc_radio_sent(void){first=0;}
static void service(void) {
    uint32_t now=RF_LinkClockUs();
    if(phase==1){if(!rfc_due(now,deadline)){arm(deadline-now);return;}phase=2;started=now;}
    if(phase==2) {
        if(!ops.drained()) {
            if(now-started<RFC_DRAIN_US){arm(10u);return;}
            phase=0;ops.ready(channel,0,generation);return;
        }
        if(!ops.apply(channel)){phase=0;ops.ready(channel,0,generation);return;}
        phase=3;arm(RFC_SETTLE_US);return;
    }
    if(phase==3){phase=0;first=1;ops.ready(channel,1,generation);}
}
__INTERRUPT
__HIGH_CODE
void TMR2_IRQHandler(void) {
    if(!TMR2_GetITFlag(TMR0_3_IT_CYC_END))return;
    TMR2_ClearITFlag(TMR0_3_IT_CYC_END);TMR2_Disable();
    /* Match the existing RF timer priority and serialize each bounded SDK
     * operation. No loop waits for settling or drain while IRQs are masked. */
    uint32_t lock;SYS_DisableAllIrq(&lock);service();SYS_RecoverIrq(lock);
}
