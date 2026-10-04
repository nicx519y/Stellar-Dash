#ifndef XORA_TX_IMAGE_BULK_H
#define XORA_TX_IMAGE_BULK_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "release_install_protocol.h"
#include "webhid_protocol.h"

/* SPI-only messages in the existing CRC/epoch/credit protected DMA lane.
 * These kinds must never be forwarded to or accepted from USB. All integers LE.
 * Header: marker, kind, version, status, magic, request ID, offset, length,
 * reserved zero bytes. Body contains only Application bytes, never IAP. */
#define XORA_TX_BULK_MARKER 0xA6u
#define XORA_TX_BULK_REQUEST 0x7Du
#define XORA_TX_BULK_RESPONSE 0x7Eu
#define XORA_TX_BULK_VERSION 1u
#define XORA_TX_BULK_MAGIC 0x32424B54u
#define XORA_TX_BULK_HEADER_BYTES 32u
#define XORA_TX_BULK_READ_BYTES (WEBHID_REPORT_BYTES-XORA_TX_BULK_HEADER_BYTES)

static inline uint16_t txb_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static inline uint32_t txb_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline void txb_put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static inline void txb_put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static inline bool txb_reserved(const uint8_t *p) {
    return p && (p[1]==XORA_TX_BULK_REQUEST || p[1]==XORA_TX_BULK_RESPONSE);
}
static inline bool txb_range(uint32_t offset,uint16_t length) {
    return length && length<=XORA_TX_BULK_READ_BYTES && offset<=XORA_TX_BACKUP_APP_BYTES &&
        length<=XORA_TX_BACKUP_APP_BYTES-offset;
}
static inline bool txb_valid(const uint8_t *p,uint8_t kind) {
    unsigned i;
    if(!p || p[0]!=XORA_TX_BULK_MARKER || p[1]!=kind || p[2]!=XORA_TX_BULK_VERSION ||
       txb_u32(p+4)!=XORA_TX_BULK_MAGIC || !txb_u32(p+8) || !txb_range(txb_u32(p+12),txb_u16(p+16))) return false;
    for(i=18;i<XORA_TX_BULK_HEADER_BYTES;++i) if(p[i]) return false;
    return kind!=XORA_TX_BULK_REQUEST || p[3]==0;
}
static inline bool txb_request(uint8_t *p,uint32_t id,uint32_t offset,uint16_t length) {
    if(!p || !id || !txb_range(offset,length)) return false;
    memset(p,0,WEBHID_REPORT_BYTES);
    p[0]=XORA_TX_BULK_MARKER;p[1]=XORA_TX_BULK_REQUEST;p[2]=XORA_TX_BULK_VERSION;
    txb_put32(p+4,XORA_TX_BULK_MAGIC);txb_put32(p+8,id);txb_put32(p+12,offset);txb_put16(p+16,length);
    return true;
}
static inline void txb_reply(uint8_t *reply,const uint8_t *request,uint8_t status) {
    memset(reply,0,WEBHID_REPORT_BYTES);memcpy(reply,request,XORA_TX_BULK_HEADER_BYTES);
    reply[1]=XORA_TX_BULK_RESPONSE;reply[3]=status;
}
static inline bool txb_matches(const uint8_t *p,uint32_t id,uint32_t offset,uint16_t length) {
    return txb_valid(p,XORA_TX_BULK_RESPONSE) && txb_u32(p+8)==id &&
        txb_u32(p+12)==offset && txb_u16(p+16)==length;
}
#endif
