#pragma once
#include <stdint.h>
#include <string.h>

// Low-frequency power snapshot, little endian, independent of input packets.
// Byte 0 version, 1 flags (valid,input,fast,estimated,low,source[6:5]),
// 2 seq u16, 4 sample uptime u32, 8 cell mV u16, 10 SOC permille u16,
// 12 input mV u16, 14 charge mA u16, 16 charge state, 17 reserved,
// 18 faults u16, 20 accumulated sample age ms u32.
#define XORA_POWER_BYTES 24u
#define XORA_POWER_VERSION 1u
#define XORA_POWER_SPI_CMD 0x0Bu
#define XORA_POWER_AUX_TYPE 12u
#define XORA_POWER_HID_MAGIC 0x31535052u /* RPS1 */
#define XORA_POWER_STALE_MS 3000u
#define XORA_POWER_AIR_BUDGET_MS 250u
static inline uint16_t xp_get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static inline uint32_t xp_get32(const uint8_t *p) { return (uint32_t)xp_get16(p) | ((uint32_t)xp_get16(p+2)<<16); }
static inline void xp_put16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8); }
static inline void xp_put32(uint8_t *p,uint32_t n) { xp_put16(p,(uint16_t)n);xp_put16(p+2,(uint16_t)(n>>16)); }
static inline int xp_valid(const uint8_t *p,unsigned n) {
    return n==XORA_POWER_BYTES && p[0]==XORA_POWER_VERSION && !(p[1]&0x80u) &&
        p[17]==0 && xp_get16(p+10)<=1000u && p[16]<=4u;
}
static inline uint32_t xp_age(uint32_t age,uint32_t extra) {
    return UINT32_MAX-age<extra ? UINT32_MAX : age+extra;
}
static inline uint32_t xp_ticks_ms(uint32_t ticks) {
    // RF_LinkClock uses 625us ticks; MS1_TO_SYSTEM_TIME(1) truncates to one!
    return (ticks/8u)*5u + (ticks%8u)*5u/8u;
}
static inline uint32_t xp_crc(const uint8_t *p,unsigned n) {
    uint32_t c=~0u;
    while(n--) { c^=*p++;for(unsigned i=0;i<8;i++) c=(c>>1)^((0u-(c&1u))&0xedb88320u); }
    return ~c;
}
