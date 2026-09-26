#ifndef RF_ACK_POLICY_H
#define RF_ACK_POLICY_H
#include <stdint.h>

/* Air v5. No rounding/saturation: invalid quality still acknowledges a link. */
#define RF_ACK_RESUME_GUARD_US 250u
static inline uint8_t rf_ack_encode(uint8_t *p,uint32_t received,uint32_t expected) {
    uint8_t valid=expected && expected<=4095u && received<=expected;
    uint32_t bits=valid ? expected | ((expected-received)<<12) : 0u;
    p[0]=(uint8_t)bits;p[1]=(uint8_t)(bits>>8);p[2]=(uint8_t)(bits>>16);
    return valid;
}
static inline uint8_t rf_ack_decode(const uint8_t *p,uint16_t *received,uint16_t *expected) {
    uint32_t bits=p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16);
    uint16_t missing=(uint16_t)(bits>>12);*expected=(uint16_t)(bits&4095u);
    if(!*expected || missing>*expected){*received=0;return 0;}
    *received=*expected-missing;return 1;
}
static inline uint32_t rf_ack_resume_at(uint32_t received,uint32_t timeout) {
    uint32_t resume=received+RF_ACK_RESUME_GUARD_US;
    return (int32_t)(resume-timeout)<0 ? resume : timeout;
}
#endif
