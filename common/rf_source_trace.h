#ifndef HBOX_RF_SOURCE_TRACE_H
#define HBOX_RF_SOURCE_TRACE_H
#include <stdint.h>

/* SPI only: v3 keeps the 10-byte input and battery/CRC offsets unchanged.
 * Bytes 6..7 were zero age in the short-air path; they now carry the full
 * immutable event ID. Do not interpret legacy v1/v2 timestamps as IDs. */
#define RF_SOURCE_INPUT_VERSION 0x30u
#define RF_SOURCE_INPUT_VERSION_MASK 0xf0u
#define RF_SOURCE_SIDECAR_VERSION 2u
#define RF_SOURCE_EVENT_OFFSET 6u
static inline __attribute__((always_inline)) uint16_t rf_source_input_event(const uint8_t *p) {
    if((p[1] & RF_SOURCE_INPUT_VERSION_MASK) != RF_SOURCE_INPUT_VERSION)return 0;
    uint16_t event=(uint16_t)p[6] | ((uint16_t)p[7]<<8);
    return event && (event & 63u)==(p[4]>>2) ? event : 0;
}
#endif
