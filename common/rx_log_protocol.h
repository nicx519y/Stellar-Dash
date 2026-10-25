#ifndef XORA_RX_LOG_PROTOCOL_H
#define XORA_RX_LOG_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#define RXL_MAGIC 0x31474c52u /* RLG1; distinct from binding/monitor control */
#define RXL_INFO 1u
#define RXL_READ 2u
#define RXL_CLOSE 3u
#define RXL_CLEAR 4u
#define RXL_DIAGNOSTICS_POLICY 5u /* read only; no RF pause, reply version/enabled */
#define RXL_BYTES 32u
#define RXL_LEASE_MS 5000u
/* Request: magic:u32, version:u8, op:u8, pending:u8, reserved:u8,
 * transaction:u32, upper:u32, before:u32, confirm:u32, reserved[6], crc16.
 * Reply: magic/version/op/status/page, transaction, payload[16], pages/length,
 * crc16. INFO=fl_info; READ=96-byte fl_record; CLEAR requires 0x434c5231.
 * A read session pauses RX input until CLOSE, USB reset, or lease expiry.
 * All Flash access and reply preparation happen in main context. */
static inline uint32_t rxl_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline void rxl_put32(uint8_t *p,uint32_t v) {
    p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;
}
static inline uint16_t rxl_crc(const uint8_t *p,size_t n) {
    uint16_t c=0xffffu;
    while(n--) { c^=(uint16_t)*p++<<8; for(unsigned b=0;b<8;b++) c=(c&0x8000u)?(uint16_t)((c<<1)^0x1021u):(uint16_t)(c<<1); }
    return c;
}
#endif
