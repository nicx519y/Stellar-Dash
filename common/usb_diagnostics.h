#pragma once
#include <stdint.h>
/* Separate read-only RPC; existing STATE/LINK_STATE/AUTH_STATUS are unchanged.
 * Request: {version,page,index}. Response: header below, then page payload.
 * All multibyte integers use little endian; timestamps wrap modulo 2^32 ms.
 * Page 0 host; 1 interface[index 0..7]; 2 auth; 3 Device; 4 fault sources;
 * 5 host device[index = address 1..7, including the hub].
 * No challenge or signature bytes are ever included. */
#define XORA_USB_DIAG_VERSION 1u
#define XORA_USB_DIAG_BYTES 56u
#define XORA_USB_DIAG_HEADER_BYTES 12u
#define XORA_USB_DIAG_HOST 0u
#define XORA_USB_DIAG_INTERFACE 1u
#define XORA_USB_DIAG_AUTH 2u
#define XORA_USB_DIAG_DEVICE 3u
#define XORA_USB_DIAG_FAULTS 4u
#define XORA_USB_DIAG_HOST_DEVICE 5u
static inline void usb_diag_put16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static inline void usb_diag_put32(uint8_t *p,uint32_t v) { usb_diag_put16(p,(uint16_t)v);usb_diag_put16(p+2,(uint16_t)(v>>16)); }
static inline uint32_t usb_diag_get32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
