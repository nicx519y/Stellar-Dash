#include "usb_monitor.h"
#include "usb_device.h"
#include "usb_webhid_memory.h"
#include "CH58x_common.h"
#include <string.h>

#define ROWS 8u
typedef struct {
    uint32_t id, previous, mask, received, armed, completed, source_sample;
    uint32_t stages[8];
    uint8_t seq, flags, revision, dirty;
    uint8_t queued;
} edge_t;
static edge_t rows[ROWS], outgoing;
static uint32_t session, next_id, received, completed, overwritten, drops, source_drops;
static uint32_t lease_at, stats_at, last_mask, clock_at, clock_token, clock_t2;
static int32_t clock_low, clock_high;
static uint16_t period_ms=250u, rate=1000u;
static uint8_t flags, baseline, inflight_row=255u, page=4u, scan;
static uint8_t inflight_input, clock_valid;
static uint32_t process_at;
static uint32_t cycles_last, micros, remainder, cycles_per_us;

/* Only main and USB ISR access this clock; SPI ISR must remain serviceable. */
USB_WEBHID_RAM uint32_t usb_monitor_now(void) {
    uint32_t lock, now, delta, result;
    lock=usb_device_irq_save();
    now=SysTick->CNTL;
    if(!cycles_per_us) { cycles_per_us=GetSysClock()/1000000u;cycles_last=now; }
    delta=now-cycles_last;cycles_last=now;
    micros+=delta/cycles_per_us;
    remainder+=delta%cycles_per_us;
    if(remainder>=cycles_per_us) { micros++;remainder-=cycles_per_us; }
    result=micros;usb_device_irq_restore(lock);return result;
}
static USB_WEBHID_RAM void changed(edge_t *r) { r->revision++;r->dirty=1u; }
void usb_monitor_reset(void) {
    /* Caller holds USB IRQ ownership on reset. Keep clock monotonic. */
    (void)usb_monitor_now(); /* Resolve the clock divisor outside the input hot path. */
    session++;if(!session)session=1u;
    memset(rows,0,sizeof(rows));flags=baseline=clock_valid=0u;
    inflight_input=0u;inflight_row=255u;
    received=completed=overwritten=drops=source_drops=next_id=0u;rate=0u;page=4u;
}
static USB_WEBHID_RAM uint8_t active(uint32_t now) {
    if(flags && now-lease_at>=UM_LEASE_US) {
        flags=0u;baseline=0u;clock_valid=0u;
        inflight_row=255u;page=4u;
    }
    return flags;
}
bool usb_monitor_control(const uint8_t *data,uint16_t length,uint8_t response[32],uint8_t speed) {
    uint32_t now=usb_monitor_now();uint8_t status=0u;
    if(length!=32u || um_u32(data)!=UM_CONTROL_MAGIC)return false;
    if(data[4]!=UM_VERSION || um_crc(data,30)!=um_u16(data+30))status=1u;
    else if(data[5]==UM_CONFIGURE) {
        uint16_t p=um_u16(data+16);
        if((data[7]&~3u) || (p!=100u && p!=250u && p!=500u && p!=1000u))status=2u;
        else {
            uint8_t next=(data[7]&UM_ENABLE)?data[7]:0u;
            if((next^flags)&UM_LATENCY) {
                baseline=0u;inflight_row=255u;
                memset(rows,0,sizeof(rows));page=4u;
            }
            flags=next;period_ms=p;lease_at=now;
        }
    } else if(data[5]==UM_RENEW) {
        if(um_u32(data+12)!=session || !active(now))status=3u;
        else lease_at=now;
    } else if(data[5]!=UM_QUERY)status=2u;
    memset(response,0,32);um_put32(response,UM_CONTROL_MAGIC);
    response[4]=UM_VERSION;response[5]=data[5];response[6]=status;response[7]=active(now);
    um_put32(response+8,um_u32(data+8));um_put32(response+12,session);
    um_put16(response+16,period_ms);response[18]=speed;response[19]=3u;
    um_put32(response+20,drops+source_drops);um_put16(response+30,um_crc(response,30));return true;
}
/* Native queue owns tokens until it drops or arms each report. A token is an
 * event ID, not the wrapping 8-bit input sequence or a recyclable row index. */
static USB_WEBHID_RAM uint32_t monitor_input_locked(const usb_board_input_v1_t *input,uint32_t now) {
    received++;
    rate=usb_board_input_rate_hz(input->flags);
    if(!(active(now)&UM_LATENCY))return 0u;
    if(!baseline) { last_mask=input->action_mask_le;baseline=1u;return 0u; }
    if(last_mask==input->action_mask_le)return 0u;
    uint8_t slot=255u;
    for(uint8_t i=0;i<ROWS;i++)if(i!=inflight_row && !rows[i].queued && !rows[i].dirty &&
        (!rows[i].id || now-rows[i].received>100000u)) { slot=i;break; }
    if(slot==255u) { drops++;last_mask=input->action_mask_le;return 0u; }
    edge_t *r=&rows[slot];usb_webhid_fill(r,0,sizeof(*r));
    usb_webhid_fill(r->stages,0xFF,sizeof(r->stages));
    if(!++next_id)++next_id;
    r->id=next_id;r->previous=last_mask;r->mask=input->action_mask_le;
    r->seq=input->seq;r->received=now;r->dirty=1u;r->queued=1u;
    last_mask=input->action_mask_le;
    return r->id;
}
/* Caller serializes native queue operations with the USB ISR. */
USB_WEBHID_RAM void usb_monitor_drop(uint32_t token,uint8_t reason) {
    if(reason==UM_OVERWRITTEN)overwritten++;
    if(!token)return;
    for(uint8_t i=0;i<ROWS;i++)if(rows[i].id==token && rows[i].queued) {
        rows[i].queued=0u;rows[i].flags|=reason;changed(&rows[i]);break;
    }
}
/* Called before EP1 ACK becomes visible, with USB IRQ masked. */
USB_WEBHID_RAM void usb_monitor_arm(uint32_t token) {
    inflight_input=1u;inflight_row=255u;
    if(!token)return;
    for(uint8_t i=0;i<ROWS;i++)if(rows[i].id==token && rows[i].queued) {
        edge_t *r=&rows[i];r->queued=0u;inflight_row=i;r->armed=usb_monitor_now();
        r->stages[4]=r->armed-r->received;changed(r);break;
    }
}
USB_WEBHID_RAM void usb_monitor_complete(void) {
    if(inflight_input)completed++;
    inflight_input=0u;
    if(inflight_row<ROWS) {
        edge_t *r=&rows[inflight_row];r->completed=usb_monitor_now();
        r->stages[5]=r->completed-r->armed;r->flags|=UM_DONE;changed(r);
    }
    inflight_row=255u;
}
static bool monitor_board_locked(const uint8_t *data,uint8_t length,uint8_t response[32],uint8_t *response_length) {
    const uint32_t now=usb_monitor_now();*response_length=0u;
    if(length<12u || data[1]!=UM_VERSION)return false;
    if(data[0]==UM_BOARD_QUERY && (length==12u || length==16u)) {
        memset(response,0,32);memcpy(response,data,12);response[2]=active(now);
        um_put32(response+4,session);um_put32(response+12,now);
        um_put32(response+16,usb_monitor_now());*response_length=20u;
        if(length==16u)source_drops=um_u32(data+12);
        clock_token=um_u32(data+8);clock_t2=now;return true;
    }
    if(um_u32(data+4)!=session || !(active(now)&UM_LATENCY))return true;
    if(data[0]==UM_BOARD_CLOCK && length==24u) {
        if(um_u32(data+8)!=clock_token || um_u32(data+20)!=clock_t2 || now-clock_t2>500000u)return true;
        int32_t lo=(int32_t)um_u32(data+12),hi=(int32_t)um_u32(data+16);
        if((int32_t)(hi-lo)<0 || (uint32_t)(hi-lo)>100000u)return true;
        clock_low=lo;clock_high=hi;clock_at=clock_t2;clock_valid=1u;return true;
    }
    if(data[0]==UM_BOARD_EDGE && length==40u) {
        edge_t *match=0;
        for(unsigned i=0;i<ROWS;i++)if(!(data[2]&(UM_UNMATCHED|UM_SEND_FAILED)) && rows[i].id && rows[i].seq==data[3] &&
            rows[i].mask==um_u32(data+16) && rows[i].previous==um_u32(data+12) && now-rows[i].received<UM_MATCH_US) {
            if(match)return true;
            match=&rows[i];
        }
        if(!match) {
            for(unsigned i=0;i<ROWS;i++)if(i!=inflight_row && !rows[i].queued && !rows[i].dirty &&
                (!rows[i].id || now-rows[i].received>100000u)) { match=&rows[i];break; }
            if(!match) { drops++;return true; }
            memset(match,0,sizeof(*match));match->id=++next_id;match->received=now;
            match->previous=um_u32(data+12);match->mask=um_u32(data+16);
            for(unsigned i=0;i<8;i++)match->stages[i]=UM_UNKNOWN;
            match->flags=UM_UNMATCHED;
        }
        match->flags|=UM_SOURCE | (data[2]&UM_SEND_FAILED);
        match->source_sample=um_u32(data+20);
        for(unsigned i=0;i<4;i++)match->stages[i]=um_u32(data+24+4*i);
        changed(match);return true;
    }
    return false;
}
bool usb_monitor_process(uint8_t speed) {
    uint32_t now=usb_monitor_now();
    if(now-process_at<1000u)return false;
    process_at=now;
    uint32_t active_lock;active_lock=usb_device_irq_save();
    const uint8_t enabled=active(now);usb_device_irq_restore(active_lock);
    if(!(enabled&UM_ENABLE))return false;
    uint8_t frame[32]={0};
    if(now-stats_at>=(uint32_t)period_ms*1000u) {
        uint32_t lock;lock=usb_device_irq_save();
        now=usb_monitor_now();
        um_put32(frame,UM_STATS_MAGIC);frame[4]=UM_VERSION;frame[5]=flags;frame[6]=speed;
        um_put32(frame+8,session);um_put32(frame+12,now);
        um_put32(frame+16,received);um_put32(frame+20,completed);um_put32(frame+24,overwritten);
        um_put16(frame+28,rate);um_put16(frame+30,drops+source_drops>65535u?65535u:(uint16_t)(drops+source_drops));
        usb_device_irq_restore(lock);
        if(usb_device_hw_send_telemetry(frame,32)) { stats_at=now;return true; }
        return false;
    }
    if(!(flags&UM_LATENCY))return false;
    if(page>=4u) {
        uint32_t lock;lock=usb_device_irq_save();
        for(unsigned j=0;j<ROWS;j++) {
            uint8_t i=(uint8_t)((scan+j)%ROWS);edge_t *r=&rows[i];
            if(!r->id)continue;
            if(!(r->flags&(UM_DONE|UM_OVERWRITTEN|UM_TIMEOUT|UM_UNMATCHED)) && now-r->received>100000u) {
                r->flags|=UM_TIMEOUT;changed(r);
            }
            if((r->flags&(UM_SOURCE|UM_DONE))==(UM_SOURCE|UM_DONE) &&
                !(r->flags&(UM_CLOCK|UM_OVERWRITTEN|UM_TIMEOUT|UM_UNMATCHED|UM_SEND_FAILED)) &&
                clock_valid && now-clock_at<UM_CLOCK_TTL_US && now-r->received<UM_CLOCK_TTL_US) {
                /* 1000 ppm relative drift plus 4 us capture/rounding margin.
                 * A conservative interval includes all unobserved SPI queue residence. */
                uint32_t margin=(now-clock_at+now-r->received)/1000u+4u;
                int32_t lo=(int32_t)(r->completed-r->source_sample-(uint32_t)clock_high)-(int32_t)margin;
                int32_t hi=(int32_t)(r->completed-r->source_sample-(uint32_t)clock_low)+(int32_t)margin;
                if(hi>=0 && hi<=1000000 && hi>=lo) {
                    r->stages[6]=lo<0?0u:(uint32_t)lo;r->stages[7]=(uint32_t)hi;r->flags|=UM_CLOCK;changed(r);
                }
            }
            if(r->dirty) { outgoing=*r;r->dirty=0u;page=0u;scan=(uint8_t)((i+1u)%ROWS);break; }
        }
        usb_device_irq_restore(lock);
    }
    if(page>=4u)return false;
    um_put32(frame,UM_EDGE_MAGIC);frame[4]=UM_VERSION;frame[5]=page;frame[6]=outgoing.revision;frame[7]=outgoing.flags;
    um_put32(frame+8,session);um_put32(frame+12,outgoing.id);um_put32(frame+16,outgoing.previous);um_put32(frame+20,outgoing.mask);
    um_put32(frame+24,outgoing.stages[page*2u]);um_put32(frame+28,outgoing.stages[page*2u+1u]);
    if(usb_device_hw_send_telemetry(frame,32)) { page++;return true; }
    return false;
}

USB_WEBHID_RAM uint32_t usb_monitor_input(const usb_board_input_v1_t *input,uint32_t now) {
    return monitor_input_locked(input,now);
}
bool usb_monitor_board(const uint8_t *data,uint8_t length,uint8_t response[32],uint8_t *response_length) {
    uint32_t lock;lock=usb_device_irq_save();
    const bool ok=monitor_board_locked(data,length,response,response_length);
    usb_device_irq_restore(lock);return ok;
}
