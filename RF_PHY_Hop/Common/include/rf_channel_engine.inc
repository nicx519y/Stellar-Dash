/* Shared transaction engine; compiled once by each role's APP module.
 * Callers serialize events. No hardware, input queue, or SDK dependency. */
#include "rf_channel_manager.h"
#include <string.h>
#ifndef RFC_MANAGER_FAILURE
#define RFC_MANAGER_FAILURE(m,now) ((void)0)
#endif
static void rfc_switch(rfc_manager_t *m,uint8_t ch,uint32_t at) {
    m->switch_channel=ch;m->switch_at=at;m->switch_pending=1;
}
#include "rf_fast_engine.inc"
void rfc_manager_init(rfc_manager_t *m,uint8_t tx,uint8_t ch,uint16_t hz,uint32_t now) {
    memset(m,0,sizeof(*m));m->tx=tx;m->hz=hz;m->profile=RFC_PROFILE_VERSION;
    m->primary=m->old=m->target=m->channel=ch;m->last_data=m->last_ack=now;
    m->wire_session=(uint8_t)(now^(now>>8)^(tx?0xa5u:0u));
    m->probe_disable=RFC_PROBE_UNACCEPTED;
}
void rfc_manager_cancel(rfc_manager_t *m) {
    m->fast_enabled=m->fast_requested=m->fast_offer=m->fast_control=m->fast_reply=0;
    m->fast_activate=m->fast_quiet_until=0;m->fast_have_data=m->fast_data_sent=m->fast_done=0;
    m->fast_state=m->fast_latched?RFF_LATCHED:RFF_OFF;
    m->state=RFC_IDLE;m->connected=0;m->armed=m->want_ack=m->switch_pending=0;
    m->reply_cmd=m->committed=m->failed=m->discovery=m->probe_result=0;
    m->have_seq=m->recovering=m->transition_tracking=0;m->session++;m->generation++;
}
void rfc_manager_connect(rfc_manager_t *m,uint8_t ch,uint32_t now) {
    rfc_manager_cancel(m);m->connected=1;m->primary=m->old=m->target=m->channel=ch;
    m->last_data=m->last_ack=now;
}
uint8_t rfc_manager_budget(rfc_manager_t *m,uint32_t now,uint32_t cost) {
    if(!m->budget_valid){m->budget_valid=1;m->budget_epoch=now;}
    uint32_t n=(now-m->budget_epoch)/10000u;
    if(n>=101){memset(m->budget,0,sizeof(m->budget));m->budget_total=0;m->budget_index=0;m->budget_epoch=now;}
    else while(n--){m->budget_epoch+=10000u;m->budget_index=(m->budget_index+1u)%101u;
        m->budget_total-=m->budget[m->budget_index];m->budget[m->budget_index]=0;}
    m->budget_used=m->budget_total;
    if(cost>RFC_BUDGET_US || m->budget_total>RFC_BUDGET_US-cost)return 0;
    m->budget[m->budget_index]+=cost;m->budget_total+=cost;m->budget_used=m->budget_total;return 1;
}
uint8_t rfc_manager_budget_available(rfc_manager_t *m,uint32_t now,uint32_t cost) {
    (void)rfc_manager_budget(m,now,0u);
    return cost<=RFC_BUDGET_US && m->budget_total<=RFC_BUDGET_US-cost;
}
uint16_t rfc_manager_probe_window(rfc_manager_t *m) {
    m->probe_disable=RFC_PROBE_UNACCEPTED;
    if((rfc_local_caps(m->hz)&(RFC_CAP_SWITCH|RFC_CAP_ACK|RFC_CAP_PROBE))!=(RFC_CAP_SWITCH|RFC_CAP_ACK|RFC_CAP_PROBE))return 0;
    m->probe_disable=RFC_PROBE_PEER;
    if((m->peer_caps&rfc_local_caps(m->hz))!=rfc_local_caps(m->hz))return 0;
    m->probe_disable=RFC_PROBE_RATE;if(m->hz<4000u)return 0;
    uint32_t slot=1000000u/m->hz;
    /* Drain is done BEFORE departure. Include both packet-boundary phases,
     * outgoing/return settling, first-packet guard, and clock skew. */
    uint32_t overhead=2u*RFC_CALLBACK_DRAIN_US+2u*RFC_SETTLE_US+2u*RFC_FIRST_PACKET_US+RFC_SKEW_US+2u*slot;
    m->probe_disable=RFC_PROBE_WINDOW;if(overhead>=1000u)return 0;
    uint32_t dwell=((1000u-overhead)/slot)*slot;if(dwell<2u*slot)return 0;
    m->probe_disable=RFC_PROBE_OK;return (uint16_t)dwell;
}
uint8_t rfc_manager_begin(rfc_manager_t *m,uint8_t target,uint8_t mode,uint16_t quality,uint32_t now) {
    if(!m->connected || rff_busy(m) || (m->fast_state>=RFF_SETTING && m->fast_state<=RFF_VERIFY_MODE) || m->state!=RFC_IDLE || rfc_index(target)==0xff || target==m->channel)return 0;
    if(mode&RFC_MODE_PROBE) {
        m->dwell=rfc_manager_probe_window(m);if(!m->dwell || (m->probe_at && !rfc_due(now,m->probe_at)))return 0;
        /* Reserve the ENTIRE worst-case excursion in addition to control
         * windows, even if some candidate DATA might arrive. */
        if(!rfc_manager_budget(m,now,1000u)){m->probe_disable=RFC_PROBE_BUDGET;return 0;}
        m->probe_at=now+1000000u;
    }
    m->old=m->primary;m->target=target;m->mode=mode;m->quality=quality;
    m->seq++;m->have_seq=1;m->state=RFC_PREPARE;m->deadline=now+RFC_PREPARE_US;
    m->retry_at=now;m->armed=m->committed=m->failed=m->recovering=0;m->want_ack=1;
    m->reason=(mode&RFC_MODE_MANUAL)?RFC_REASON_MANUAL:(mode&RFC_MODE_ROLLBACK)?RFC_REASON_ROLLBACK:
        (mode&RFC_MODE_EMERGENCY)?RFC_REASON_EMERGENCY:RFC_REASON_BAD;
    return 1;
}
uint8_t rfc_manager_control(rfc_manager_t *m,uint8_t *p,uint32_t now) {
    uint8_t cmd=m->state==RFC_PREPARE?RFH_CMD_HOP_PREPARE:m->state==RFC_CONFIRM?RFH_CMD_HOP_CONFIRM:
        m->state==RFC_RECOVER?RFC_CMD_RECOVER:0;
    if(!cmd || !rfc_due(now,m->retry_at))return 0;
    memset(p,0,10);p[0]=cmd;p[1]=m->target;p[4]=m->seq;p[5]=m->old;
    p[8]=RFC_PROFILE_VERSION|(m->wire_session&0xf0u);
    p[9]=m->mode|((m->wire_session&15u)<<4);
    rfh_put_u16(p+6,(m->mode&RFC_MODE_PROBE)?m->dwell:m->quality);
    if(cmd==RFH_CMD_HOP_CONFIRM) {
        if(!m->armed){m->activate_at=now+RFC_ACTIVATE_LEAD_US;m->armed=1;}
        if(rfc_due(now,m->activate_at))return 0;
        rfh_put_u16(p+2,(uint16_t)(m->activate_at-now));
    }
    return cmd;
}
void rfc_manager_sent(rfc_manager_t *m,uint8_t cmd,uint32_t now) {
    if(cmd)m->retry_at=now+(cmd==RFH_CMD_HOP_CONFIRM?2000u:RFC_RETRY_US);
    m->want_ack=0;
}
static void rfc_recover(rfc_manager_t *m,uint32_t now) {
    if(m->state!=RFC_RECOVER) {
        if(!m->recovering){m->recovering=1;m->recovery_deadline=now+RFC_RECOVER_US;m->failures++;}
        m->state=RFC_RECOVER;
        m->fast_index=0;m->fast_at=now;m->retry_at=now;
    }
}
uint8_t rfc_manager_command(rfc_manager_t *m,const uint8_t *p,uint8_t ch,uint32_t now) {
    if(rff_busy(m) || (m->fast_state>=RFF_SETTING && m->fast_state<=RFF_VERIFY_MODE))return 0;
    uint8_t cmd=p[0],target=p[1],seq=p[4],old=p[5],mode=p[9]&15u;
    uint8_t session=(p[8]&0xf0u)|(p[9]>>4);
    if(m->tx || !m->connected || (m->recovering && rfc_due(now,m->recovery_deadline)) ||
       (p[8]&15u)!=RFC_PROFILE_VERSION || session!=m->wire_session || rfc_index(target)==0xff ||
       rfc_index(old)==0xff || target==old || (mode&~15u))return 0;
    if((mode&RFC_MODE_PROBE) && (!rfc_manager_probe_window(m) || rfh_get_u16(p+6)>rfc_manager_probe_window(m)))return 0;
    if(cmd==RFH_CMD_HOP_PREPARE) {
        if(ch!=old)return 0;
        if(m->have_seq && seq==m->seq) {
            if(m->old!=old || m->target!=target || m->mode!=mode || m->state==RFC_IDLE ||
               rfc_due(now,m->deadline))return 0;
        } else {
            if(m->have_seq && (uint8_t)(seq-m->seq)>=128u)return 0;
            if(m->state!=RFC_IDLE)return 0;
            m->old=old;m->target=target;m->seq=seq;m->have_seq=1;m->mode=mode;
            m->dwell=(mode&RFC_MODE_PROBE)?rfh_get_u16(p+6):0;m->quality=rfh_get_u16(p+6);
            m->state=RFC_PREPARE;m->deadline=now+RFC_PREPARE_US;m->armed=m->recovering=0;
        }
    } else if(cmd==RFH_CMD_HOP_CONFIRM) {
        if(!m->have_seq || seq!=m->seq || old!=m->old || target!=m->target || mode!=m->mode || ch!=old ||
           (m->state!=RFC_PREPARE && m->state!=RFC_ARMED) || rfc_due(now,m->deadline))return 0;
        uint16_t delay=rfh_get_u16(p+2);if(delay<1000u || delay>RFC_ACTIVATE_LEAD_US)return 0;
        /* Set once: repeated packets can never extend the activation time. */
        if(!m->armed){m->activate_at=now+delay;m->return_at=m->activate_at+RFC_SETTLE_US+m->dwell;
            m->deadline=now+RFC_CONFIRM_US;
            m->armed=1;m->state=RFC_ARMED;}
    } else if(cmd==RFC_CMD_RECOVER) {
        if(seq!=m->seq || old!=m->old || target!=m->target || mode!=m->mode ||
           (ch!=old && ch!=target) || !m->have_seq)return 0;
        /* The peer may have lost the final ACK after we committed on DATA.
         * Acknowledge that SAME completed transaction without starting it again. */
        if(m->state==RFC_IDLE) {
            if(ch!=target || m->primary!=target)return 0;
            m->reply_cmd=cmd;m->reply_seq=seq;return 1;
        }
        if(ch==target){if(m->state!=RFC_VERIFY)m->deadline=now+RFC_CONFIRM_US;m->state=RFC_VERIFY;}
        else {rfc_recover(m,now);}
    } else return 0;
    m->reply_cmd=cmd;m->reply_seq=seq;return 1;
}
void rfc_manager_ack(rfc_manager_t *m,uint8_t cmd,uint8_t txn,uint8_t ch,uint32_t now) {
    if(!m->connected || ch!=m->channel)return;
    m->last_ack=now;
    if(m->state==RFC_VERIFY && ch==m->target) {
        m->primary=ch;m->state=RFC_IDLE;m->committed=1;m->recovering=0;return;
    }
    if(txn!=m->seq)return;
    if(m->state==RFC_PREPARE && cmd==RFH_CMD_HOP_PREPARE && ch==m->old && !rfc_due(now,m->deadline)) {
        m->state=RFC_CONFIRM;m->deadline=now+RFC_CONFIRM_US;m->retry_at=now;m->want_ack=1;
    } else if(m->state==RFC_CONFIRM && cmd==RFH_CMD_HOP_CONFIRM && ch==m->old && !rfc_due(now,m->activate_at)) {
        m->state=RFC_ARMED;m->return_at=m->activate_at+RFC_SETTLE_US+m->dwell;
    } else if(m->state==RFC_RECOVER && cmd==RFC_CMD_RECOVER && ch==m->target) {
        m->primary=ch;m->state=RFC_IDLE;m->committed=1;m->recovering=0;
    }
}
#ifdef RFC_HIGH_CODE
RFC_HIGH_CODE
#endif
void rfc_manager_data(rfc_manager_t *m,uint8_t recovery,uint8_t ch,uint16_t gap,uint32_t now) {
    if(!m->connected)return;
    m->last_gap=now-m->last_data;if(m->last_gap>m->max_gap)m->max_gap=m->last_gap;
    m->last_data=now;
    if(m->transition_tracking){m->first_packet_at=now-m->ready_at;
        m->transition_gap=now-m->transition_started;m->transition_tracking=0;}
    if(m->state!=RFC_IDLE)m->transition_missing+=gap;
    if(m->state==RFC_PROBE){if(ch==m->target)m->probe_received++;return;}
    rff_data(m,recovery,ch,now);
    if(!m->tx && !recovery && ((m->state==RFC_VERIFY && ch==m->target) ||
        (m->state==RFC_RECOVER && ch==m->target))) {
        m->old=m->primary;m->target=ch;m->primary=ch;m->state=RFC_IDLE;m->committed=1;m->recovering=0;
    }
}
void rfc_manager_timeout(rfc_manager_t *m,uint32_t now) { rff_timeout(m,now); }
void rfc_manager_poll(rfc_manager_t *m,uint32_t now) {
    if(!m->connected)return;
    if(m->recovering && rfc_due(now,m->recovery_deadline)){m->failed=1;m->discovery=1;return;}
    if(m->probe_at && rfc_due(now,m->probe_at))m->probe_at=0;
    if(m->state==RFC_IDLE)return;
    if(m->state==RFC_PREPARE || m->state==RFC_CONFIRM) {
        if(rfc_due(now,m->deadline)) {
            m->reservation_failures++;
            m->reason=m->state==RFC_PREPARE?RFC_REASON_PREPARE_TIMEOUT:RFC_REASON_CONFIRM_TIMEOUT;
            if(m->state==RFC_PREPARE){m->state=RFC_IDLE;m->failed=1;m->failures++;}
            else rfc_recover(m,now);
        } else if(m->tx && m->state==RFC_CONFIRM && m->armed && rfc_due(now,m->activate_at))rfc_recover(m,now);
        else if(m->tx && rfc_due(now,m->retry_at))m->want_ack=1;
    }
    if(m->state==RFC_ARMED) {
        /* Queue the actual switch in a hardware timer, not the 625us loop. */
        rfc_switch(m,m->target,m->activate_at+(m->tx?RFC_SKEW_US:0u));
        m->state=(m->mode&RFC_MODE_PROBE)?RFC_PROBE:RFC_VERIFY;
        m->deadline=m->activate_at+RFC_CONFIRM_US;m->probe_sent=m->probe_received=0;
    }
    /* Probe return is armed by the radio-ready adapter in TMR2. It must not
     * wait for this housekeeping loop or cancel an in-progress return. */
    if(m->state==RFC_RETURN && m->channel==m->old) {
        if(m->tx){if(rfc_due(now,m->retry_at)){m->want_ack=1;m->retry_at=now+RFC_RETRY_US;}
            if(rfc_due(now,m->deadline)){m->state=RFC_IDLE;m->probe_failures++;m->probe_result=2;}}
        else {m->state=RFC_IDLE;m->probe_result=1;}
    }
    if(m->state==RFC_VERIFY) {
        if(rfc_due(now,m->deadline))rfc_recover(m,now);
        else if(m->tx && rfc_due(now,m->retry_at)){m->want_ack=1;m->retry_at=now+2000u;}
    }
    if(m->state==RFC_RECOVER) {
        if(rfc_due(now,m->recovery_deadline)){m->failed=1;m->discovery=1;return;}
        if(rfc_due(now,m->fast_at)) {
            m->fast_index^=1;rfc_switch(m,m->fast_index?m->target:m->old,now);
            m->fast_at=now+(m->tx?10000u:2000u);m->retry_at=now;m->want_ack=m->tx;
        }
    }

}
uint8_t rfc_manager_switch(rfc_manager_t *m,uint8_t *ch,uint32_t *at) {
    if(!m->switch_pending)return 0;
    m->switch_pending=0;*ch=m->switch_channel;*at=m->switch_at;return 1;
}
void rfc_manager_radio_ready(rfc_manager_t *m,uint8_t ch,uint32_t generation,uint32_t now) {
    m->channel=ch;m->generation=generation;m->switches++;
    m->before_gap=m->last_gap;m->transition_started=m->tx?now:m->last_data;
    m->ready_at=now;m->transition_tracking=1;
}
