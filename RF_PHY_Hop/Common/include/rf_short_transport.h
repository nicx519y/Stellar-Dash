#ifndef RF_SHORT_TRANSPORT_H
#define RF_SHORT_TRANSPORT_H
#include <stdint.h>
#include <string.h>

/* v2 DATA always contains the complete 18-bit input state. The upper six
 * state bits identify a measured edge; they are never mapped to buttons. */
#define RFH_SHORT_LEN 5u
#define RFH_AUX_LEN 7u
#define RFH_SHORT_ACK_LEN 5u
#define RFH_AUX_BYTES 64u
#define RFH_AUX_PASSES 3u
#define RFH_AUX_BATTERY 1u
#define RFH_AUX_STATS 2u
#define RFH_AUX_CONFIG 3u
#define RFH_AUX_TRACE 4u
#define RFH_AUX_RATE 5u
#define RFH_AUX_SOURCE_DIAG 10u /* 7 u32 counters; SPI source archive, v1 */
#define RFH_SHORT_ACK_VERSION 0x80u
#define RFH_MEASUREMENT_FLAG 0x20u
typedef struct { uint8_t data[64], index, pass, generation; volatile uint8_t active; uint32_t serial; } rfh_aux_tx_t;
typedef struct { uint8_t data[64], generation, active; uint64_t seen; uint32_t serial; uint8_t delivered; } rfh_aux_rx_t;
static inline uint32_t rfh_aux_crc(const uint8_t *p, unsigned n) {
    uint32_t c=0xffffffffu;
    while(n--) { c^=*p++; for(unsigned i=0;i<8;i++) c=(c>>1)^((0u-(c&1u))&0xedb88320u); }
    return ~c;
}
static inline void rfh_short_put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i)); }
static inline uint32_t rfh_short_get32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline int rfh_is_short(uint8_t n) { return n==RFH_SHORT_LEN || n==RFH_AUX_LEN; }
/* Records are immutable across all three passes. Commit only after launch
 * succeeds: rejected DMA launches must not silently consume a fragment. */
static inline int rfh_aux_begin(rfh_aux_tx_t *s,uint8_t type,const uint8_t *p,uint8_t n) {
    if(s->active || n>54u)return 0;
    memset(s->data,0,64); s->data[0]=type; s->data[1]=n;
    rfh_short_put32(s->data+2,++s->serial); if(n)memcpy(s->data+6,p,n);
    rfh_short_put32(s->data+6u+n,rfh_aux_crc(s->data,6u+n));
    s->generation=(uint8_t)((s->generation+1u)&3u); s->index=s->pass=0;
    __asm__ volatile("" ::: "memory");
    s->active=1; return 1;
}
static inline int rfh_aux_peek(const rfh_aux_tx_t *s,uint8_t *p) {
    if(!s->active)return 0;
    p[0]=(uint8_t)((s->generation<<6)|s->index);p[1]=s->data[s->index];return 1;
}
static inline void rfh_aux_commit(rfh_aux_tx_t *s) {
    if(!s->active)return;
    if(++s->index==10u+s->data[1]){s->index=0;if(++s->pass==RFH_AUX_PASSES)s->active=0;}
}
static inline int rfh_aux_receive(rfh_aux_rx_t *s,const uint8_t *p) {
    uint8_t g=p[0]>>6,i=p[0]&63u;
    if(!s->active || s->generation!=g){s->seen=0;s->active=1;s->generation=g;s->delivered=0;}
    /* A generation wraps after four records. Changed header bytes invalidate
     * old coverage instead of mixing a skipped record with the new one. */
    if(i<6u && (s->seen&((uint64_t)1u<<i)) && s->data[i]!=p[1]){s->seen=0;s->delivered=0;}
    s->data[i]=p[1];s->seen|=(uint64_t)1u<<i;
    if((s->seen&3u)!=3u || s->delivered || s->data[1]>54u)return 0;
    uint8_t length=10u+s->data[1];
    uint64_t required=length==64u ? UINT64_MAX : (((uint64_t)1u<<length)-1u);
    if((s->seen&required)!=required)return 0;
    if(rfh_short_get32(s->data+length-4u)!=rfh_aux_crc(s->data,length-4u))return 0;
    s->serial=rfh_short_get32(s->data+2);s->delivered=1;return 1;
}
#endif
