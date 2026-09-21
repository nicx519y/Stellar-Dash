#include "rf_channel_policy.h"
#include <string.h>
void rfc_policy_init(rfc_policy_t *p,uint8_t channel,uint32_t now) {
    memset(p,0,sizeof(*p));p->loss=p->baseline=RFC_UNKNOWN;p->probe_candidate=0xff;
    for(unsigned i=0;i<RFH_HOP_CHANNEL_COUNT;i++)p->history[i].loss=RFC_UNKNOWN;
    rfc_policy_channel(p,channel,now);
}
void rfc_policy_channel(rfc_policy_t *p,uint8_t channel,uint32_t now) {
    p->channel=channel;p->window_at=now;p->received=p->expected=0;
    p->bad=p->good=p->emergency=p->window_invalid=0;p->loss=RFC_UNKNOWN;
    memset(p->recent_rx,0,sizeof(p->recent_rx));memset(p->recent_expected,0,sizeof(p->recent_expected));
}
void rfc_policy_sample(rfc_policy_t *p,uint8_t channel,uint16_t rx,uint16_t expected,uint32_t now) {
    if(channel!=p->channel || !expected || rx>expected)return;
    p->received+=rx;p->expected+=expected;p->last_sample=now;
    if(rfc_loss(rx,expected)>=500u){if(p->emergency<3)p->emergency++;}else p->emergency=0;
}
void rfc_policy_failed(rfc_policy_t *p,uint8_t target,uint32_t now) {
    uint8_t i=rfc_index(target);if(i!=0xff)p->history[i].quarantine=now+RFC_QUARANTINE_US;
    p->cooldown=now+RFC_COOLDOWN_US;
}
void rfc_policy_poll(rfc_policy_t *p,uint32_t now) {
    if(p->cooldown && rfc_due(now,p->cooldown))p->cooldown=0;
    if(p->emergency_after && rfc_due(now,p->emergency_after))p->emergency_after=0;
    if(p->explore_after && rfc_due(now,p->explore_after))p->explore_after=0;
    for(unsigned i=0;i<RFH_HOP_CHANNEL_COUNT;i++) {
        rfc_history_t *h=&p->history[i];
        if(h->quarantine && rfc_due(now,h->quarantine))h->quarantine=0;
        if(h->windows && now-h->measured>=RFC_HISTORY_US){h->windows=0;h->loss=RFC_UNKNOWN;}
        if(h->probes && now-h->probe_first>=RFC_HISTORY_US){h->probes=0;h->probe_received=h->probe_expected=0;}
    }
    if(now-p->window_at>=1000000u) {
        uint8_t complete=now-p->window_at<1500000u && p->expected!=0 && !p->window_invalid;
        p->loss=complete ? rfc_loss(p->received,p->expected):RFC_UNKNOWN;
        uint8_t i=rfc_index(p->channel);
        p->recent_rx[p->recent_index]=complete?p->received:0;
        p->recent_expected[p->recent_index]=complete?p->expected:0;
        p->recent_index=(p->recent_index+1u)%3u;
        if(i!=0xff && complete) {
            rfc_history_t *h=&p->history[i];h->received=h->expected=0;
            for(unsigned k=0;k<3;k++){h->received+=p->recent_rx[k];h->expected+=p->recent_expected[k];}
            h->loss=rfc_loss(h->received,h->expected);h->measured=now;if(h->windows<65535u)h->windows++;
        }
        if(complete && p->loss>=50){if(p->bad<3)p->bad++;p->good=0;}
        else {p->bad=0;if(complete && p->loss<20){if(p->good<2)p->good++;}else p->good=0;}
        if(p->probation && complete) {
            p->probation_rx+=p->received;p->probation_expected+=p->expected;p->probation_windows++;
            if(p->baseline!=RFC_UNKNOWN && p->loss>=p->baseline+20u)p->worse++;else p->worse=0;
        }
        p->received=p->expected=0;p->window_at=now;p->window_invalid=0;
    }
    if(p->probation && (p->worse>=2 || rfc_due(now,p->probation_until))) {
        uint8_t ok=p->worse<2 && p->probation_windows>=2 &&
            rfc_improved(p->baseline,rfc_loss(p->probation_rx,p->probation_expected));
        p->reason=ok?RFC_REASON_ACCEPTED:(p->probation_windows<2?RFC_REASON_INSUFFICIENT:RFC_REASON_NO_IMPROVEMENT);
        p->probation=0;p->cooldown=now+RFC_COOLDOWN_US;
        if(!ok){rfc_policy_failed(p,p->channel,now);p->rollback=1;}
    }
}
uint8_t rfc_policy_choose(rfc_policy_t *p,uint32_t now,uint8_t *reason) {
    if(p->rollback){*reason=RFC_REASON_ROLLBACK;return p->probation_old;}
    if(p->probation)return p->channel;
    uint8_t emergency=p->emergency>=3 && !p->emergency_after && now-p->last_sample<500000u;
    if(!emergency && (p->bad<3 || p->cooldown))return p->channel;
    *reason=emergency?RFC_REASON_EMERGENCY:RFC_REASON_BAD;
    uint8_t best=p->channel;uint16_t best_loss=RFC_UNKNOWN;
    for(uint8_t i=0;i<RFH_HOP_CHANNEL_COUNT;i++) {
        rfc_history_t *h=&p->history[i];uint8_t ch=rfh_hop_channel_at(i);
        if(ch==p->channel || (h->quarantine && !rfc_due(now,h->quarantine)))continue;
        if(h->windows && now-h->measured<RFC_HISTORY_US && rfc_improved(p->loss,h->loss) && h->loss<best_loss)
            {best=ch;best_loss=h->loss;}
        if(h->probes>=8 && h->probe_expected>=64 && h->probe_last-h->probe_first>=2000000u &&
           now-h->probe_first<RFC_HISTORY_US && rfc_improved(p->loss,rfc_loss(h->probe_received,h->probe_expected)) &&
           rfc_loss(h->probe_received,h->probe_expected)<best_loss)
            {best=ch;best_loss=rfc_loss(h->probe_received,h->probe_expected);}
    }
    if(best!=p->channel)return best;
    if(!emergency && p->probe_candidate!=0xff) {
        uint8_t i=rfc_index(p->probe_candidate);
        if(i!=0xff && (!p->history[i].quarantine || rfc_due(now,p->history[i].quarantine)))return p->probe_candidate;
    }
    if(!emergency && p->explore_after)return p->channel;
    for(unsigned k=0;k<RFH_HOP_CHANNEL_COUNT;k++) {
        uint8_t i=(p->cursor+k)%RFH_HOP_CHANNEL_COUNT; rfc_history_t *h=&p->history[i];
        if(rfh_hop_channel_at(i)!=p->channel && (!h->quarantine || rfc_due(now,h->quarantine)) &&
           (!h->windows || now-h->measured>=RFC_HISTORY_US))return rfh_hop_channel_at(i);
    }
    return p->channel;
}
void rfc_policy_started(rfc_policy_t *p,uint8_t target,uint8_t reason,uint32_t now) {
    /* Freeze the baseline before control reservations alter residence windows. */
    uint32_t rx=0,expected=0;
    for(unsigned k=0;k<3;k++){rx+=p->recent_rx[k];expected+=p->recent_expected[k];}
    if(!expected){rx=p->received;expected=p->expected;}
    if(reason!=RFC_REASON_ROLLBACK)p->baseline=rfc_loss(rx,expected);
    p->reason=reason;p->cursor=(rfc_index(target)+1u)%RFH_HOP_CHANNEL_COUNT;
    if(reason==RFC_REASON_EMERGENCY){p->emergency_after=now+5000000u;p->emergency=0;}
    if(reason==RFC_REASON_ROLLBACK)p->rollback=0;
    p->explore_after=now+RFC_COOLDOWN_US;
}
void rfc_policy_committed(rfc_policy_t *p,uint8_t old,uint8_t target,uint8_t mode,uint32_t now) {
    p->probation_old=old;
    p->probation=!(mode&(RFC_MODE_MANUAL|RFC_MODE_ROLLBACK|RFC_MODE_RECOVERY));
    p->probation_until=now+RFC_PROBATION_US;p->probation_rx=p->probation_expected=0;
    p->probation_windows=p->worse=0;p->probe_candidate=0xff;
    rfc_policy_channel(p,target,now);p->cooldown=now+RFC_COOLDOWN_US;
}
void rfc_policy_probe(rfc_policy_t *p,uint8_t ch,uint16_t rx,uint16_t sent,uint32_t now) {
    uint8_t i=rfc_index(ch);if(i==0xff || !sent || rx>sent)return;
    rfc_history_t *h=&p->history[i];
    if(!h->probes || now-h->probe_first>=RFC_HISTORY_US){h->probe_first=now;h->probes=0;h->probe_received=h->probe_expected=0;}
    h->probes++;h->probe_last=now;h->probe_received+=rx;h->probe_expected+=sent;
    if(!rx){rfc_policy_failed(p,ch,now);p->probe_candidate=0xff;}
    else if(h->probes>=8 && h->probe_expected>=64 && now-h->probe_first>=2000000u &&
            !rfc_improved(p->loss,rfc_loss(h->probe_received,h->probe_expected))) {
        rfc_policy_failed(p,ch,now);p->probe_candidate=0xff;p->reason=RFC_REASON_NO_IMPROVEMENT;
    }
}
