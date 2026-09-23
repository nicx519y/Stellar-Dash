#ifndef HBOX_RF_BINDING_PROTOCOL_H
#define HBOX_RF_BINDING_PROTOCOL_H
#include <stdint.h>
#include <string.h>

/* USB-only provisioning. Never transported on the RF data plane. All words LE. */
#define RFB_REQUEST_MAGIC 0x31504252UL /* RBP1 */
#define RFB_RESPONSE_MAGIC 0x31534252UL /* RBS1 */
#define RFB_PAGE_MAGIC 0x31484252UL /* RBH1 */
#define RFB_VERSION 1u
#define RFB_REQUEST_SIZE 32u
#define RFB_RESPONSE_SIZE 48u
#define RFB_GET_ACTIVE 1u
#define RFB_GET_PENDING 2u
#define RFB_PREPARE 3u
#define RFB_COMMIT 4u
#define RFB_ABORT 5u
#define RFB_OK 0u
#define RFB_INVALID 1u
#define RFB_CONFLICT 2u
#define RFB_STORAGE 3u
#define RFB_UNSUPPORTED 4u
#define RFB_BUSY 5u
#define RFB_EXHAUSTED 6u
#define RFB_PRESENT 1u
#define RFB_WEB 2u
#define RFB_PENDING 4u
#define RFB_FIXED 8u
#define RFB_CAPABILITIES 1u

static inline uint32_t rfb_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline void rfb_put(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static inline uint32_t rfb_checksum(const uint8_t *p,unsigned n) {
    uint32_t h=2166136261UL;unsigned i;
    for(i=0;i<n;i++){h^=p[i];h*=16777619UL;}return h;
}
/* Request: magic/version/op/sequence16, transaction, expectedRevision,
 * peerId, address, bindingGeneration, checksum. Response: header, status,
 * flags, reserved16, localId, peerId, address, bindingGeneration,
 * transaction, revision, capabilities, reserved32, checksum. */
static inline uint8_t rfb_request_valid(const uint8_t *p) {
    return rfb_u32(p)==RFB_REQUEST_MAGIC && p[4]==RFB_VERSION &&
        p[5]>=RFB_GET_ACTIVE && p[5]<=RFB_ABORT && rfb_u32(p+28)==rfb_checksum(p,28);
}
static inline void rfb_response_init(uint8_t *out,const uint8_t *req,uint8_t status) {
    memset(out,0,RFB_RESPONSE_SIZE);rfb_put(out,RFB_RESPONSE_MAGIC);
    out[4]=RFB_VERSION;out[5]=req[5];out[6]=req[6];out[7]=req[7];out[8]=status;
    rfb_put(out+36,RFB_CAPABILITIES);
}
static inline void rfb_response_finish(uint8_t *out) {rfb_put(out+44,rfb_checksum(out,44));}
#endif
