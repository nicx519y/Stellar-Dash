#ifndef XORA_USB_MONITOR_H
#define XORA_USB_MONITOR_H
#include <stdbool.h>
#include "diagnostics_policy.h"
#include "usb_board_link_protocol.h"
#include "usb_monitor_protocol.h"
void usb_monitor_reset(void);
void usb_monitor_policy_reset(void);
uint32_t usb_monitor_now(void);
bool usb_monitor_control(const uint8_t *data, uint16_t length, uint8_t response[32], uint8_t speed);
bool usb_monitor_board(const uint8_t *data, uint8_t length, uint8_t response[32], uint8_t *response_length);
/* Queue caller holds IRQ ownership for input/drop/arm. Zero means no edge. */
uint32_t usb_monitor_input(const usb_board_input_v1_t *input, uint32_t now);
void usb_monitor_drop(uint32_t token, uint8_t reason);
void usb_monitor_arm(uint32_t token);
void usb_monitor_complete(void);
bool usb_monitor_process(uint8_t speed); /* true when EP7 consumed this turn */
#ifndef XORA_USB_MONITOR_IMPLEMENTATION
#define usb_monitor_input(...) (XORA_DIAGNOSTICS_ENABLED ? usb_monitor_input(__VA_ARGS__) : 0u)
#define usb_monitor_process(...) (XORA_DIAGNOSTICS_ENABLED && usb_monitor_process(__VA_ARGS__))
#define usb_monitor_board(...) (XORA_DIAGNOSTICS_ENABLED && usb_monitor_board(__VA_ARGS__))
#define usb_monitor_drop(...) XORA_DIAGNOSTIC_CALL(usb_monitor_drop(__VA_ARGS__))
#define usb_monitor_arm(...) XORA_DIAGNOSTIC_CALL(usb_monitor_arm(__VA_ARGS__))
#define usb_monitor_complete(...) XORA_DIAGNOSTIC_CALL(usb_monitor_complete(__VA_ARGS__))
#endif
#endif
