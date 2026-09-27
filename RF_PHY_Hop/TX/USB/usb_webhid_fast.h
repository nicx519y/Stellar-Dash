#ifndef XORA_USB_WEBHID_FAST_H
#define XORA_USB_WEBHID_FAST_H
#include "usb_board_link_protocol.h"
#include "webhid_protocol.h"
#include <stdbool.h>
void usb_webhid_fast_reset(void);
void usb_webhid_fast_fault(uint8_t port_fault);
void usb_webhid_fast_port_detail(uint32_t detail, uint32_t produced, uint32_t consumed);
void usb_webhid_fast_process(void);
bool usb_webhid_fast_feed(uint8_t byte);
uint16_t usb_webhid_fast_feed_block(const uint8_t *data, uint16_t size);
bool usb_webhid_fast_submit(const uint8_t *report);
bool usb_webhid_fast_ready(void);
void usb_webhid_fast_capability(webhid_capability_v2_t *cap, uint8_t speed);
uint8_t usb_webhid_fast_control(uint8_t opcode, const uint8_t *data,
    uint8_t size, uint8_t *response, uint8_t *response_size);
bool usb_board_link_port_set_fast_webhid(bool enabled);
bool usb_board_link_port_queue_block(const uint8_t *frame, uint16_t length);
#endif
