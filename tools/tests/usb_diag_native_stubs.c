/* Deterministic read-only telemetry providers for native management tests. */
#include "usb_diagnostics_tx.h"
#include "usb_host.h"
uint32_t usb_host_clock_snapshot(void) { return 1234; }
uint32_t usb_host_generation(void) { return 7; }
uint16_t usb_host_diag(uint8_t p,uint8_t i,uint8_t *o,uint16_t n) {(void)p;(void)i;(void)n;o[0]=1;return 1;}
uint16_t usb_auth_host_diag(uint8_t *o) {o[0]=1;return 1;}
uint16_t usb_device_hw_diag(uint8_t *o) {o[0]=1;return 1;}
