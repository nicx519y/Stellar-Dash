#ifndef XORA_WEBHID_FAST_LINK_H
#define XORA_WEBHID_FAST_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "webhid_protocol.h"

#ifndef WHF_COPY
#define WHF_COPY memcpy
#endif
#ifndef WHF_CRC_CODE
#define WHF_CRC_CODE static inline
#endif

/* Independent of the 0x5A boot/IAP/control framing. All wire integers LE. */
#define WHF_SYNC 0x5Bu
#define WHF_HEADER_BYTES 32u
#define WHF_BATCH 3u
#define WHF_BLOCK_BYTES (WHF_HEADER_BYTES + WHF_BATCH * WEBHID_REPORT_BYTES)
#define WHF_CAPACITY WEBHID_REPORT_WINDOW
#define WHF_DATA_WINDOW (WHF_CAPACITY - 1u)
#define WHF_COUNTER_LIMIT 0xFFFFFF00u

typedef struct {
    uint32_t epoch, tx_block, rx_block;
    uint32_t tx_produced, tx_sent, tx_acked, peer_limit;
    uint32_t rx_accepted, rx_released;
    uint32_t crc_errors, protocol_errors, duplicates;
    uint32_t last_rx_crc;
    uint8_t dirty, failed;
    uint8_t tx[WHF_CAPACITY][WEBHID_REPORT_BYTES];
    uint8_t rx[WHF_CAPACITY][WEBHID_REPORT_BYTES];
} whf_link_t;

static inline uint16_t whf_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t whf_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline void whf_put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void whf_put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
WHF_CRC_CODE uint32_t whf_crc_update(uint32_t crc, const uint8_t *p, uint16_t n) {
    static const uint32_t table[16] = {
        0x00000000u,0x1DB71064u,0x3B6E20C8u,0x26D930ACu,
        0x76DC4190u,0x6B6B51F4u,0x4DB26158u,0x5005713Cu,
        0xEDB88320u,0xF00F9344u,0xD6D6A3E8u,0xCB61B38Cu,
        0x9B64C2B0u,0x86D3D2D4u,0xA00AE278u,0xBDBDF21Cu };
    while(n-- != 0u) {
        crc ^= *p++;
        crc = (crc >> 4) ^ table[crc & 15u];
        crc = (crc >> 4) ^ table[crc & 15u];
    }
    return crc;
}
static inline uint32_t whf_crc(const uint8_t *block, uint16_t size) {
    uint32_t crc = whf_crc_update(0xFFFFFFFFu, block, 28u);
    return ~whf_crc_update(crc, block + WHF_HEADER_BYTES, (uint16_t)(size - WHF_HEADER_BYTES));
}
static inline void whf_init(whf_link_t *s, uint32_t epoch) {
    memset(s, 0, sizeof(*s)); s->epoch=epoch; s->peer_limit=WHF_CAPACITY; s->dirty=1u;
}
static inline bool whf_is_control(const uint8_t *report) {
    return report[1]==WEBHID_REPORT_BOOTSTRAP_REQUEST || report[1]==WEBHID_REPORT_BOOTSTRAP_RESPONSE ||
        report[1]==WEBHID_REPORT_SECURE_REQUEST || report[1]==WEBHID_REPORT_SECURE_RESPONSE || report[1]==0x7Fu;
}
static inline bool whf_enqueue(whf_link_t *s, const uint8_t *report) {
    if(s->epoch==0u || s->failed || !report || s->tx_produced>=WHF_COUNTER_LIMIT ||
       s->tx_produced-s->tx_acked >= (whf_is_control(report)?WHF_CAPACITY:WHF_DATA_WINDOW)) return false;
    WHF_COPY(s->tx[s->tx_produced % WHF_CAPACITY], report, WEBHID_REPORT_BYTES);
    ++s->tx_produced; return true;
}
static inline const uint8_t *whf_peek(const whf_link_t *s) {
    return s->rx_released < s->rx_accepted ? s->rx[s->rx_released % WHF_CAPACITY] : 0;
}
static inline void whf_release(whf_link_t *s) {
    if(s->rx_released < s->rx_accepted) { ++s->rx_released; s->dirty=1u; }
}
/* Prepare does not consume anything. Commit only after the port owns a copy. */
static inline uint16_t whf_prepare(const whf_link_t *s, uint8_t *block) {
    uint32_t available=s->tx_produced-s->tx_sent;
    uint32_t credit=s->peer_limit>s->tx_sent ? s->peer_limit-s->tx_sent : 0u;
    uint8_t count=(uint8_t)(available>WHF_BATCH ? WHF_BATCH : available), i;
    uint16_t size;
    if(s->failed || !s->epoch || s->tx_block>=WHF_COUNTER_LIMIT) return 0u;
    if(count>credit) count=(uint8_t)credit;
    for(i=0u;i<count;++i) {
        if(credit-i==1u && !whf_is_control(s->tx[(s->tx_sent+i)%WHF_CAPACITY])) { count=i; break; }
    }
    if(!count && !s->dirty) return 0u;
    size=(uint16_t)(WHF_HEADER_BYTES + count*WEBHID_REPORT_BYTES);
    memset(block,0,WHF_HEADER_BYTES);
    block[0]=WHF_SYNC; block[1]=2u; whf_put16(block+2,size);
    whf_put32(block+4,s->epoch); whf_put32(block+8,s->tx_block+1u);
    whf_put32(block+12,s->tx_sent); whf_put32(block+16,s->rx_accepted);
    whf_put32(block+20,s->rx_released+WHF_CAPACITY); block[24]=count;
    for(i=0u;i<count;++i) WHF_COPY(block+WHF_HEADER_BYTES+i*WEBHID_REPORT_BYTES,
        s->tx[(s->tx_sent+i)%WHF_CAPACITY],WEBHID_REPORT_BYTES);
    whf_put32(block+28,whf_crc(block,size)); return size;
}
static inline void whf_commit(whf_link_t *s, const uint8_t *block) {
    s->tx_sent+=block[24]; ++s->tx_block; s->dirty=0u;
}
static inline bool whf_accept(whf_link_t *s, const uint8_t *b, uint16_t size) {
    uint32_t seq, first, ack, limit; uint8_t count, i;
    if(size<WHF_HEADER_BYTES || size>WHF_BLOCK_BYTES || b[0]!=WHF_SYNC || b[1]!=2u ||
       whf_u16(b+2)!=size || whf_u32(b+4)!=s->epoch || !s->epoch || s->failed ||
       b[25] || whf_u16(b+26) || b[24]>WHF_BATCH ||
       size!=WHF_HEADER_BYTES+b[24]*WEBHID_REPORT_BYTES) goto bad;
    if(whf_u32(b+28)!=whf_crc(b,size)) { ++s->crc_errors; return false; }
    seq=whf_u32(b+8); first=whf_u32(b+12); ack=whf_u32(b+16); limit=whf_u32(b+20); count=b[24];
    if(seq==s->rx_block && seq!=0u) {
        if(whf_u32(b+28)!=s->last_rx_crc) goto bad;
        ++s->duplicates; return true;
    }
    if(seq!=s->rx_block+1u || seq>=WHF_COUNTER_LIMIT || first!=s->rx_accepted ||
       ack<s->tx_acked || ack>s->tx_sent || limit<s->peer_limit ||
       limit<ack || limit-ack>WHF_CAPACITY ||
       s->rx_accepted-s->rx_released+count>WHF_CAPACITY ||
       first+count>=WHF_COUNTER_LIMIT) goto bad;
    for(i=0u;i<count;++i) WHF_COPY(s->rx[(s->rx_accepted+i)%WHF_CAPACITY],
        b+WHF_HEADER_BYTES+i*WEBHID_REPORT_BYTES,WEBHID_REPORT_BYTES);
    s->rx_accepted+=count; s->rx_block=seq; s->tx_acked=ack; s->peer_limit=limit;
    s->last_rx_crc=whf_u32(b+28);
    if(count) s->dirty=1u;
    return true;
bad:
    ++s->protocol_errors; s->failed=1u; return false;
}

#endif
