#ifndef XORA_USB_MONITOR_PROTOCOL_H
#define XORA_USB_MONITOR_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>

/* Independent of RF CTL1 and Windows HBC1. All integers are little endian.
 * HID input/feature reports remain 32 bytes, without a report ID on the wire. */
#define UM_CONTROL_MAGIC 0x31434d55u /* UMC1 */
#define UM_STATS_MAGIC   0x31534d55u /* UMS1 */
#define UM_EDGE_MAGIC    0x31454d55u /* UME1 */
#define UM_VERSION 1u
#define UM_BYTES 32u
#define UM_ENABLE 1u
#define UM_LATENCY 2u
#define UM_QUERY 0u
#define UM_CONFIGURE 1u
#define UM_RENEW 2u
#define UM_LEASE_US 3000000u
#define UM_CLOCK_TTL_US 2000000u
#define UM_MATCH_US 20000u /* less than an 8-bit sequence wrap at 8 kHz */
#define UM_UNKNOWN 0xffffffffu
#define UM_SOURCE 1u
#define UM_DONE 2u
#define UM_CLOCK 4u
#define UM_OVERWRITTEN 8u
#define UM_TIMEOUT 16u
#define UM_SEND_FAILED 32u
#define UM_UNMATCHED 64u
#define UM_OVERFLOW 128u
#define UM_BOARD_COMMAND 0x0au
#define UM_BOARD_EVENT 0x8bu
#define UM_BOARD_QUERY 0u
#define UM_BOARD_CLOCK 1u
#define UM_BOARD_EDGE 2u

/* Feature: magic[0], version[4], opcode[5], status[6], flags[7],
 * transaction[8], session[12], period_ms:u16[16], speed[18], caps[19],
 * drops:u32[20], reserved[24..29], CRC16[30] over bytes 0..29.
 * Stats: magic, version/flags/speed/reserved, session, device_us,
 * received, completed, overwritten, rate:u16, diagnostic_drops:u16.
 * Edge: magic, version/page/revision/flags, session, event, previous,
 * current, two u32 values. Pages 0 ADC/logic, 1 SPI-wait/SPI,
 * 2 CH585 receive-to-arm/arm-to-complete, 3 total lower/upper (us).
 * Four pages form one revision; UNKNOWN is not a zero duration.
 * Board messages use the existing BoardLink frame checksum: op/version/flags/seq,
 * session:u32[4], transaction/event:u32[8], then payload.
 * QUERY: cumulative source diagnostic drops[12]. Reply echoes header, t2[12], t3[16].
 * CLOCK: signed CH585-minus-STM32 lower[12], upper[16], query t2[20].
 * EDGE: previous[12], current[16], sample_us[20], ADC[24], logic[28],
 * SPI-wait[32], SPI[36]. seq is the original input sequence.
 */
static inline uint32_t um_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static inline uint16_t um_u16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1]<<8); }
static inline void um_put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static inline void um_put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static inline uint16_t um_crc(const uint8_t *p, size_t n) {
    uint16_t c=0xffffu;
    while(n--) { c^=(uint16_t)*p++<<8;for(unsigned b=0;b<8;b++)c=(c&0x8000u)?(uint16_t)((c<<1)^0x1021u):(uint16_t)(c<<1); }
    return c;
}
#endif
