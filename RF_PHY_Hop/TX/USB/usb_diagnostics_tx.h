#pragma once
#include "usb_diagnostics.h"
#include <stdbool.h>
bool usb_diagnostics_query(uint8_t page, uint8_t index, uint8_t out[56], uint8_t *length);
/* source 0: SPI port (detail=overflow cause); 1: BoardLink (detail=command).
 * Current means pending delivery, history survives acknowledgement. */
void usb_diag_fault_record(uint8_t source, uint8_t code, uint8_t detail);
void usb_diag_fault_ack(uint8_t source);
uint32_t usb_host_clock_snapshot(void);
uint16_t usb_auth_host_diag(uint8_t *out);
uint16_t usb_device_hw_diag(uint8_t *out);
