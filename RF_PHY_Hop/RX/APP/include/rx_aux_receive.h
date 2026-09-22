#ifndef RX_AUX_RECEIVE_H
#define RX_AUX_RECEIVE_H
#include "rf_short_transport.h"
/* RX-only equivalent of the v5 auxiliary reassembler. Use two 32-bit masks:
 * variable 64-bit shifts on RV32 otherwise call slow Flash libgcc helpers on
 * every fragment. The wire format, CRC, duplicate and generation rules match. */
static inline __attribute__((always_inline)) int rx_aux_receive(rfh_aux_rx_t *s,const uint8_t *p) {
    uint8_t g=p[0]>>6,i=p[0]&63u;
    uint32_t lo=(uint32_t)s->seen,hi=(uint32_t)(s->seen>>32);
    if(!s->active || s->generation!=g){lo=hi=0;s->active=1;s->generation=g;s->delivered=0;}
    uint32_t bit=1u<<(i&31u);
    if(i<6u && (lo&bit) && s->data[i]!=p[1]){lo=hi=0;s->delivered=0;}
    s->data[i]=p[1];if(i<32u)lo|=bit;else hi|=bit;
    s->seen=((uint64_t)hi<<32)|lo;
    if((lo&3u)!=3u || s->delivered || s->data[1]>54u)return 0;
    uint8_t length=10u+s->data[1];
    uint32_t need_lo=length>=32u?UINT32_MAX:(1u<<length)-1u;
    uint32_t need_hi=length<=32u?0:length==64u?UINT32_MAX:(1u<<(length-32u))-1u;
    if((lo&need_lo)!=need_lo || (hi&need_hi)!=need_hi)return 0;
    if(rfh_short_get32(s->data+length-4u)!=rfh_aux_crc(s->data,length-4u))return 0;
    s->serial=rfh_short_get32(s->data+2);s->delivered=1;return 1;
}
#endif
