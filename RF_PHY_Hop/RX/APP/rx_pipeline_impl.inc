/* Included by RF_PHY.c after the pure report mapper. All queue operations are
 * serialized by the short IRQ mask; USB owns its independent DMA copy. */
#define RFQ_CODE __HIGH_CODE
#include "rx_report_queue.h"
static rfq_t g_pipe;
static uint32_t g_pipe_generation,g_pipe_last_drop,g_pipe_drop_generation;
static uint8_t g_pipe_payload[RF_INPUT_PAYLOAD_LEN],g_pipe_latest_valid,g_pipe_metadata_dirty;
static uint8_t g_pipe_usb_was_ready;

__HIGH_CODE static void pipe_drop(const rfq_report_t *r,uint8_t reset){
    RXP_Count(reset?RP_CANCELLED:RP_CONGESTION);
    RXP_Drop(r->id,r->generation,reset);
    g_pipe_last_drop=r->id;g_pipe_drop_generation=r->generation;
    if(r->tag && r->session==g_relative_session){relative_rx_t *t=&g_relative_rx[r->tag];
        if(t->row==r->row && !(t->flags&4u)){t->flags|=8u;short_dirty(t);}}
}
__HIGH_CODE static void pipe_generation(void){
    if(g_pipe_generation==g_demo_radio_generation)return;
    rfq_cancel(&g_pipe,pipe_drop);g_pipe_generation=g_demo_radio_generation;
    g_pipe_latest_valid=0;g_pipe_metadata_dirty=0;
}
__HIGH_CODE static void pipe_kick(void){
    uint32_t key;SYS_DisableAllIrq(&key);pipe_generation();
    if(!USBHS_DevEnumStatus || (USBHS_DevSleepStatus&2u) || g_pipe.busy ||
       (USBHS_Endp_Busy[DEF_UEP2]&DEF_UEP_BUSY) || !g_pipe.count){SYS_RecoverIrq(key);return;}
    /* On resume retain the newest queued state, but never replay it with a
     * fabricated timestamp. Old age remains visible in the queue-wait metric. */
    rfq_report_t *r=&g_pipe.pending[g_pipe.head];uint32_t now=SysTick->CNT;
    if(!r->neutral && g_pipe.count>1 && now-r->ready_cycles>g_demo_cycles_per_us*500u){
        while(g_pipe.count>1){pipe_drop(&g_pipe.pending[g_pipe.head],0);rfq_pop(&g_pipe);}
        r=&g_pipe.pending[g_pipe.head];
    }
    if(r->generation!=g_demo_radio_generation || r->epoch!=g_demo_input_epoch){
        pipe_drop(r,1);rfq_pop(&g_pipe);SYS_RecoverIrq(key);return;}
    if(USBHS_Endp_DataUp(DEF_UEP2,r->bytes,20,DEF_UEP_CPY_LOAD)==0){
        r->submit_cycles=SysTick->CNT;rfq_copy(&g_pipe.flight,r);g_pipe.busy=1;
        RXP_Count(RP_SUBMITTED);RXP_Time(RT_QUEUE_WAIT,r->submit_cycles-r->ready_cycles,r->id,r->generation);
        if(r->neutral)g_demo_neutral_pending=0;
        rfq_pop(&g_pipe);
    } else RXP_Count(RP_SUBMIT_FAILED);
    SYS_RecoverIrq(key);
}
__HIGH_CODE static void pipe_accept(const rf_rx_pending_t *p){
    uint8_t payload[RF_INPUT_PAYLOAD_LEN];rfq_report_t report;
    demo_decode_short_input_payload(payload,p->air[1],p->air+2);
    RXP_Time(RT_PARSE,SysTick->CNT-p->profile_cycles,p->profile_id,p->generation);
    rfq_clear(&report);
    demo_build_xinput_report(payload,report.bytes);
    uint32_t key;SYS_DisableAllIrq(&key);pipe_generation();
    if(p->generation!=g_demo_radio_generation || g_demo_radio_reconfiguring ||
       g_demo_rx_state==RF_AUTO_RX_UNCONNECTED || g_demo_rx_state==RF_AUTO_RX_CONNECT_ACK_PENDING){
        RXP_Count(RP_REJECTED);SYS_RecoverIrq(key);return;}
    short_rx_edge(p,TMR0_GetCurrentTimer());
    report.id=p->profile_id;report.generation=p->generation;report.epoch=g_demo_input_epoch;
    report.rx_cycles=p->profile_cycles;report.rx_tmr=p->rx_tmr;
    report.ready_cycles=SysTick->CNT;report.ready_tmr=TMR0_GetCurrentTimer();
    if(g_short_measure){uint8_t tag=payload[4]>>2;relative_rx_t *r=&g_relative_rx[tag];
        /* Only the first report owns this edge. A later duplicate dropped
         * during USB congestion must not invalidate an earlier in-flight edge. */
        if(tag && r->tag==tag && !(r->flags&28u)){
            report.tag=tag;report.row=r->row;report.session=g_relative_session;
            r->ready=report.ready_tmr;r->flags|=16u;
        }}
    RXP_Count(RP_ACCEPTED);RXP_Count(RP_READY);g_demo_short_decoded++;
    RXP_Time(RT_READY,report.ready_cycles-report.rx_cycles,report.id,report.generation);
    demo_copy_bytes(g_pipe_payload,payload,sizeof(payload));g_pipe_latest_valid=1;g_pipe_metadata_dirty=1;
    g_demo_last_input_tmr=p->rx_tmr;g_demo_have_valid_input=1;g_demo_input_stale=0;
    rfq_push(&g_pipe,&report,report.ready_cycles,g_demo_cycles_per_us*500u,pipe_drop);
    SYS_RecoverIrq(key);pipe_kick();
}
/* Legacy/full control input still uses the same endpoint owner. */
static void pipe_legacy_input(const uint8_t *payload,uint32_t rx_tmr,uint32_t process_tmr){
    (void)process_tmr;rfq_report_t r;rfq_clear(&r);
    if(!demo_build_xinput_report(payload,r.bytes))return;
    uint32_t key;SYS_DisableAllIrq(&key);pipe_generation();
    r.id=++g_profile_serial;r.generation=g_demo_radio_generation;r.epoch=g_demo_input_epoch;
    r.rx_tmr=rx_tmr;r.ready_tmr=TMR0_GetCurrentTimer();r.ready_cycles=SysTick->CNT;
    r.rx_cycles=r.ready_cycles-demo_tmr0_elapsed_cycles(rx_tmr,r.ready_tmr);
    RXP_Count(RP_ACCEPTED);RXP_Count(RP_READY);
    RXP_Time(RT_READY,r.ready_cycles-r.rx_cycles,r.id,r.generation);
    demo_copy_bytes(g_pipe_payload,payload,RF_INPUT_PAYLOAD_LEN);g_pipe_latest_valid=g_pipe_metadata_dirty=1;
    g_demo_last_input_tmr=rx_tmr;g_demo_have_valid_input=1;g_demo_input_stale=0;
    rfq_push(&g_pipe,&r,r.ready_cycles,g_demo_cycles_per_us*500u,pipe_drop);
    SYS_RecoverIrq(key);pipe_kick();
}
__HIGH_CODE static void pipe_neutral(const uint8_t *bytes){
    rfq_report_t r;rfq_clear(&r);
    uint32_t key;SYS_DisableAllIrq(&key);pipe_generation();rfq_cancel(&g_pipe,pipe_drop);
    demo_copy_bytes(r.bytes,bytes,20);r.neutral=1;r.id=++g_profile_serial;
    r.epoch=g_demo_input_epoch;r.generation=g_demo_radio_generation;
    r.rx_cycles=r.ready_cycles=SysTick->CNT;r.rx_tmr=r.ready_tmr=TMR0_GetCurrentTimer();
    g_pipe_latest_valid=0;g_pipe_metadata_dirty=0;
    rfq_push(&g_pipe,&r,r.ready_cycles,g_demo_cycles_per_us*500u,pipe_drop);
    SYS_RecoverIrq(key);pipe_kick();
}
__HIGH_CODE static void pipe_complete(uint32_t tick){
    uint32_t key;SYS_DisableAllIrq(&key);
    if(g_pipe.busy){rfq_report_t *r=&g_pipe.flight;
        if(rfq_current(r,g_demo_radio_generation,g_demo_input_epoch)){
            RXP_Count(RP_COMPLETED);RXP_Time(RT_IN,SysTick->CNT-r->submit_cycles,r->id,r->generation);
            if(r->tag && r->session==g_relative_session){relative_rx_t *t=&g_relative_rx[r->tag];
                if(t->row==r->row && !(t->flags&12u)){t->done=tick;t->flags|=4u;short_dirty(t);}}
        } else if(!r->cancelled)pipe_drop(r,1);
        g_pipe.busy=0;
    }
    SYS_RecoverIrq(key);
}
__HIGH_CODE static void pipe_reset(void){
    uint32_t key;SYS_DisableAllIrq(&key);rfq_cancel(&g_pipe,pipe_drop);
    g_pipe.busy=0;g_pipe_usb_was_ready=0;SYS_RecoverIrq(key);
}
__HIGH_CODE static void pipe_service(void){
    uint8_t payload[RF_INPUT_PAYLOAD_LEN],dirty;
    uint32_t key;SYS_DisableAllIrq(&key);pipe_generation();
    dirty=g_pipe_metadata_dirty;g_pipe_metadata_dirty=0;
    if(dirty)demo_copy_bytes(payload,g_pipe_payload,sizeof(payload));
    /* Radio continues producing while USB is disconnected. Queued latest state
     * supplies re-enumeration; no stale pre-reset in-flight identity is reused. */
    if(USBHS_DevEnumStatus && !g_pipe_usb_was_ready && !g_pipe.count){
        rfq_report_t r;rfq_clear(&r);r.bytes[1]=20;
        r.neutral=!g_pipe_latest_valid || g_demo_input_stale;
        if(!r.neutral)demo_build_xinput_report(g_pipe_payload,r.bytes);
        r.id=++g_profile_serial;r.generation=g_demo_radio_generation;r.epoch=g_demo_input_epoch;
        r.ready_cycles=r.rx_cycles=SysTick->CNT;r.ready_tmr=r.rx_tmr=TMR0_GetCurrentTimer();
        rfq_push(&g_pipe,&r,r.ready_cycles,g_demo_cycles_per_us*500u,pipe_drop);
    }
    g_pipe_usb_was_ready=USBHS_DevEnumStatus;
    if(dirty)demo_capture_xinput_metadata(payload);
    RXP_State(!USBHS_DevEnumStatus?0:(USBHS_DevSleepStatus&2u)?3:USBHS_DevSpeed==USBHS_SPEED_HIGH?2:1,
        g_pipe.count,g_pipe.highwater,1,g_demo_radio_generation,g_pipe_last_drop,g_pipe_drop_generation);
    SYS_RecoverIrq(key);pipe_kick();
}
