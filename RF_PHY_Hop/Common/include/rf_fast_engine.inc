/* Same production state machine is used by the discrete event runner.
 * All calls are serialized by the adapter. No SDK, USB, or mutable DMA. */
uint8_t rff_enabled(const rfc_manager_t *m){return m->fast_enabled && !m->fast_latched && rff_supported(m->hz);}
uint8_t rff_busy(const rfc_manager_t *m){return m->fast_state>=RFF_RETRY && m->fast_state<=RFF_COMMITTING;}
void rff_fail(rfc_manager_t *m,uint8_t reason,uint32_t now){
    if(m->fast_latched && m->discovery)return;
    m->fast_reason=reason;m->fast_failure++;m->fast_enabled=0;m->fast_latched=1;
    m->fast_state=RFF_LATCHED;m->fast_elapsed=now-m->fast_started;
    m->want_ack=m->switch_pending=m->fast_control=m->fast_reply=0;m->discovery=1;
    RFC_MANAGER_FAILURE(m,now);
}
void rff_request(rfc_manager_t *m,uint8_t enabled,uint8_t seq,uint32_t now){
    if(!m->connected || (enabled && !rff_timing_fits(m->hz)))return;
    if(seq==m->fast_seq && m->fast_requested==enabled && m->fast_state!=RFF_OFF && !m->fast_latched)return;
    if(m->state!=RFC_IDLE || rff_busy(m))return;
    m->fast_requested=enabled;m->fast_seq=seq;m->fast_latched=0;m->fast_reason=0;
    m->fast_started=now;m->fast_deadline=now+1500000u;m->fast_next=now;
    m->fast_state=RFF_SETTING;m->fast_control=RFF_SET;m->fast_have_data=0;m->fast_activate=0;
}
uint8_t rff_control(rfc_manager_t *m,uint8_t *p,uint32_t now){
    if(!m->tx || !m->fast_control || !rfc_due(now,m->fast_next))return 0;
    if(m->fast_control==RFF_COMMIT_LINK && !m->fast_data_sent)return 0;
    uint8_t cmd=m->fast_control;
    memset(p,0,10);p[0]=cmd;p[1]=m->fast_requested;p[4]=m->fast_seq;
    p[5]=m->fast_primary;p[8]=m->wire_session;p[9]=RFF_PROFILE;
    if(cmd==RFF_ARM){
        if(!m->fast_activate)m->fast_activate=now+5000u;
        if(rfc_due(now,m->fast_activate))return 0;
        rfh_put_u16(p+2,(uint16_t)(m->fast_activate-now));
    }
    if(cmd==RFF_COMMIT_LINK){p[1]=m->fast_target;p[5]=m->fast_primary;}
    return cmd;
}
uint8_t rff_receive(rfc_manager_t *m,const uint8_t *p,uint8_t ch,uint32_t now){
    uint8_t cmd=p[0],seq=p[4];
    if(m->tx || !m->connected || p[8]!=m->wire_session || p[9]!=RFF_PROFILE || ch!=m->channel)return 0;
    if(cmd==RFF_SET){
        if(p[1]>1 || (p[1] && !rff_timing_fits(m->hz)) || m->state!=RFC_IDLE || rff_busy(m))return 0;
        if(seq!=m->fast_seq || m->fast_state==RFF_OFF || m->fast_state==RFF_LATCHED){
            if(!m->fast_offer || seq!=m->fast_offer_seq || p[1]!=m->fast_requested)return 0;
            m->fast_seq=seq;m->fast_latched=0;m->fast_started=now;m->fast_deadline=now+1500000u;
            m->fast_state=RFF_SETTING;m->fast_activate=0;
        } else if(rfc_due(now,m->fast_deadline))return 0;
    } else if(cmd==RFF_ARM){
        if(seq!=m->fast_seq || p[1]!=m->fast_requested ||
           (m->fast_state!=RFF_SETTING && m->fast_state!=RFF_SCHEDULED))return 0;
        uint16_t delay=rfh_get_u16(p+2);if(delay<1000u || delay>5000u)return 0;
        if(!m->fast_activate){m->fast_activate=now+delay;m->fast_state=RFF_SCHEDULED;m->fast_deadline=m->fast_activate+100000u;}
    } else if(cmd==RFF_COMMIT_MODE){
        if(seq!=m->fast_seq || p[1]!=m->fast_requested ||
           (m->fast_state!=RFF_VERIFY_MODE && m->fast_state!=RFF_ACTIVE && m->fast_state!=RFF_OFF))return 0;
        m->fast_enabled=m->fast_requested;m->fast_state=m->fast_enabled?RFF_ACTIVE:RFF_OFF;
        m->fast_offer=0;m->last_data=now;
    } else if(cmd==RFF_COMMIT_LINK){
        /* A second recovery on the same primary can start before the prior
         * duplicate-ACK grace expires. Its next sequence, not DATA key values,
         * distinguishes it from a retry of the completed commit. */
        if(rff_enabled(m) && p[1]==ch && now-m->fast_gap<=RFF_SILENCE_US &&
           m->fast_state==RFF_ACTIVE && m->fast_have_data && ch==m->primary &&
           p[5]==m->primary && seq==(uint8_t)(m->fast_seq+1u)){
            m->fast_primary=m->primary;m->fast_started=m->fast_gap;
            m->fast_deadline=m->fast_started+RFF_RX_LIMIT_US;m->fast_state=RFF_HELD;
        }
        if(!rff_enabled(m) || p[1]!=ch || !m->fast_have_data || p[5]!=m->fast_primary ||
           (m->fast_state!=RFF_HELD && m->fast_state!=RFF_ACTIVE))return 0;
        if(m->fast_state==RFF_HELD){
            if(rfc_due(now,m->fast_deadline) || seq!=(uint8_t)(m->fast_seq+1u))return 0;
            m->fast_seq=seq;m->old=m->primary;m->primary=ch;m->target=ch;
            m->mode=RFC_MODE_RECOVERY;m->committed=1;m->fast_state=RFF_ACTIVE;
            m->fast_success++;m->fast_elapsed=now-m->fast_started;m->fast_done=1;
        } else if(seq!=m->fast_seq || ch!=m->primary)return 0;
    } else return 0;
    m->fast_reply=cmd;m->fast_reply_seq=seq;return 1;
}
void rff_ack(rfc_manager_t *m,uint8_t cmd,uint8_t seq,uint8_t ch,uint32_t now){
    if(!m->tx || ch!=m->channel)return;
    if(cmd==RFF_SET && seq==m->fast_seq && m->fast_state==RFF_SETTING){
        m->fast_state=RFF_ARMING;m->fast_control=RFF_ARM;m->fast_activate=0;m->fast_next=now;
    } else if(cmd==RFF_ARM && seq==m->fast_seq && m->fast_state==RFF_ARMING && !rfc_due(now,m->fast_activate)){
        m->fast_state=RFF_SCHEDULED;m->fast_control=0;m->fast_deadline=m->fast_activate+100000u;
    } else if(cmd==RFF_COMMIT_MODE && seq==m->fast_seq && m->fast_state==RFF_VERIFY_MODE){
        m->fast_enabled=m->fast_requested;m->fast_state=m->fast_enabled?RFF_ACTIVE:RFF_OFF;
        m->fast_control=0;m->fast_activate=0;m->last_data=now;m->fast_next=now+RFF_PERIOD_US;m->fast_poll_at=now+1000000u;
    } else if(cmd==RFF_COMMIT_LINK && seq==m->fast_seq && m->fast_state==RFF_COMMITTING){
        m->old=m->primary;m->primary=ch;m->target=ch;m->mode=RFC_MODE_RECOVERY;m->committed=1;
        m->fast_state=RFF_ACTIVE;m->fast_control=0;m->fast_success++;
        m->fast_elapsed=now-m->fast_started;m->fast_done=1;m->fast_next=now+RFF_PERIOD_US;
    } else if(!rff_command(cmd) && rff_busy(m) && m->fast_state!=RFF_COMMITTING){
        if(ch==m->fast_primary && m->fast_state==RFF_RETRY){
            /* RX may already be HELD; complete the same commit even when
             * recovery succeeded on the original channel. */
        }
        m->fast_target=ch;m->fast_state=RFF_COMMITTING;m->fast_control=RFF_COMMIT_LINK;
        m->fast_seq++;m->fast_data_sent=0;m->fast_next=now+1000u;
    }
}
void rff_data(rfc_manager_t *m,uint8_t recovery,uint8_t ch,uint32_t now){
    if(!rff_enabled(m) || m->tx)return;
    if(recovery){
        m->fast_gap=now;
        if(ch!=m->primary && ch!=rfc_backup(m->primary,0) && ch!=rfc_backup(m->primary,1))return;
        if(m->fast_state==RFF_ACTIVE && m->fast_have_data && ch==m->primary &&
           !rfc_due(now,m->fast_deadline))return; /* duplicate after commit ACK loss */
        if(!rff_busy(m)){
            m->fast_started=now;m->fast_deadline=now+RFF_RX_LIMIT_US;m->fast_primary=m->primary;m->fast_count++;
        }
        m->fast_target=ch;m->fast_state=RFF_HELD;m->fast_have_data=1;
    } else if(m->fast_state==RFF_SCAN && ch==m->fast_primary){
        m->fast_state=RFF_ACTIVE;m->fast_have_data=0;
    }
}
void rff_timeout(rfc_manager_t *m,uint32_t now){
    if(!m->tx || !rff_enabled(m) || m->state!=RFC_IDLE)return;
    if(m->fast_state==RFF_ACTIVE){
        m->fast_state=RFF_RETRY;m->fast_primary=m->primary;m->fast_index_v4=0;
        m->fast_started=m->request_at;m->fast_deadline=m->fast_started+RFF_TX_LIMIT_US;
        m->fast_next=m->request_at+RFF_RETRY_US;m->fast_count++;
    } else if(m->fast_state==RFF_RETRY){
        if(!m->auto_enabled){rff_fail(m,RFF_RECOVERY_TIMEOUT,now);return;}
        m->fast_state=RFF_SCAN;m->fast_index_v4=1;m->fast_next=now+RFF_DWELL_US;
        m->fast_target=rfc_backup(m->fast_primary,0);rfc_switch(m,m->fast_target,now);
        m->retry_at=now;
    }
}
void rff_poll(rfc_manager_t *m,uint32_t now){
    if(!m->connected)return;
    if(m->fast_enabled && !rff_timing_fits(m->hz)){rff_fail(m,RFF_UNSUPPORTED,now);return;}
    if(m->fast_state==RFF_ARMING && m->fast_activate && rfc_due(now,m->fast_activate)){
        rff_fail(m,RFF_MODE_TIMEOUT,now);return;
    }
    if((m->fast_state>=RFF_SETTING && m->fast_state<=RFF_VERIFY_MODE) || rff_busy(m)){
        if(rfc_due(now,m->fast_deadline)){rff_fail(m,rff_busy(m)?RFF_RECOVERY_TIMEOUT:RFF_MODE_TIMEOUT,now);return;}
    }
    if(m->fast_state==RFF_SCHEDULED && rfc_due(now,m->fast_activate)){
        m->fast_state=RFF_VERIFY_MODE;m->fast_next=now;
        if(m->tx)m->fast_control=RFF_COMMIT_MODE;
    }
    if(m->tx && m->fast_control && rfc_due(now,m->fast_next))m->want_ack=1;
    if(!rff_enabled(m) || m->state!=RFC_IDLE)return;
    if(m->tx){
        if(m->fast_state==RFF_ACTIVE && !m->fast_control && rfc_due(now,m->fast_next)){
            m->want_ack=1;
            if(rfc_due(now,m->fast_poll_at)){m->fast_control=RFF_POLL;m->fast_next=now;}
            else m->fast_next=now+RFF_PERIOD_US;
        } else if(m->fast_state==RFF_RETRY && rfc_due(now,m->fast_next)){
            m->want_ack=1;m->fast_next=m->fast_deadline;
        } else if(m->fast_state==RFF_SCAN){
            if(rfc_due(now,m->fast_next)){
                if(m->fast_index_v4>=2){rff_fail(m,RFF_RECOVERY_TIMEOUT,now);return;}
                m->fast_index_v4=2;m->fast_target=rfc_backup(m->fast_primary,1);
                rfc_switch(m,m->fast_target,now);m->fast_next+=RFF_DWELL_US;
            }
            if(rfc_due(now,m->retry_at)){m->want_ack=1;m->retry_at=now+1000u;}
        }
    } else {
        uint32_t silence=m->last_data+RFF_SILENCE_US;
        if((int32_t)(m->fast_quiet_until-silence)>0)silence=m->fast_quiet_until;
        /* A request ACK can be lost in one direction. Do not pin RX on the
         * first rendezvous until the 80ms total deadline while TX moves on. */
        if(m->fast_state==RFF_HELD && m->auto_enabled && rfc_due(now,silence)){
            m->fast_state=RFF_SCAN;m->fast_have_data=0;
            m->fast_index_v4=m->channel==m->fast_primary?0u:
                m->channel==rfc_backup(m->fast_primary,0)?1u:2u;
            m->fast_next=now+(m->fast_index_v4?4000u:2000u);
        }
        if(m->fast_state==RFF_ACTIVE && rfc_due(now,silence)){
            if(!m->auto_enabled)return;
            m->fast_state=RFF_SCAN;m->fast_primary=m->primary;m->fast_started=now;
            m->fast_have_data=0;
            m->fast_deadline=now+RFF_RX_LIMIT_US;m->fast_next=now+2000u;
            m->fast_index_v4=0;m->fast_count++;
        }
        if(m->fast_state==RFF_SCAN && rfc_due(now,m->fast_next)){
            m->fast_index_v4=(m->fast_index_v4+1u)%3u;
            uint8_t ch=m->fast_index_v4?rfc_backup(m->fast_primary,m->fast_index_v4-1u):m->fast_primary;
            rfc_switch(m,ch,now);m->fast_next+=m->fast_index_v4?4000u:2000u;
        }
    }
}
void rff_sent(rfc_manager_t *m,uint8_t cmd,uint32_t now){
    if(cmd==RFF_POLL){m->fast_control=0;m->fast_poll_at=now+1000000u;m->fast_next=now+RFF_PERIOD_US;}
    else if(rff_command(cmd))m->fast_next=now+10000u;
    else if(m->fast_state==RFF_COMMITTING)m->fast_data_sent=1;
}
uint32_t rff_next(const rfc_manager_t *m,uint32_t now){
    if(!m->connected || m->fast_latched)return now+1000000u;
    uint32_t next=now+1000000u;
#define EARLIER(t) do { uint32_t v=(t); if((int32_t)(v-next)<0)next=v; } while(0)
    if(m->fast_state>=RFF_SETTING && m->fast_state<=RFF_VERIFY_MODE){
        EARLIER(m->fast_deadline);if(m->fast_state==RFF_SCHEDULED)EARLIER(m->fast_activate);
        else if(m->tx && m->fast_control && !m->want_ack)EARLIER(m->fast_next);
    }
    if(rff_busy(m)){
        EARLIER(m->fast_deadline);
        if(m->fast_state==RFF_RETRY || m->fast_state==RFF_SCAN)EARLIER(m->fast_next);
        if(m->tx && m->fast_state==RFF_SCAN)EARLIER(m->retry_at);
        if(m->tx && m->fast_control && !m->want_ack)EARLIER(m->fast_next);
        if(!m->tx && m->fast_state==RFF_HELD && m->auto_enabled){
            uint32_t t=m->last_data+RFF_SILENCE_US;
            if((int32_t)(m->fast_quiet_until-t)>0)t=m->fast_quiet_until;
            EARLIER(t);
        }
    } else if(rff_enabled(m) && m->fast_state==RFF_ACTIVE){
        if(m->tx){if(!m->want_ack)EARLIER(m->fast_next);}
        else if(m->auto_enabled){uint32_t t=m->last_data+RFF_SILENCE_US;
            if((int32_t)(m->fast_quiet_until-t)>0)t=m->fast_quiet_until;
            EARLIER(t);}
    }
#undef EARLIER
    return rfc_due(now,next)?now+2u:next;
}
