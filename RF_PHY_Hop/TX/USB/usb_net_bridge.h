#ifndef TX_USB_NET_BRIDGE_H
#define TX_USB_NET_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_board_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*usb_net_bridge_sink_t)(usb_board_channel_t channel,
                                      const uint8_t *data,
                                      uint16_t length);

void usb_net_bridge_init(void);
void usb_net_bridge_reset_channel(usb_board_channel_t channel);
void usb_net_bridge_set_sink(usb_net_bridge_sink_t sink);
void usb_net_bridge_process(void);
bool usb_net_bridge_fragment(const usb_board_fragment_header_v1_t *header,
                             const uint8_t *data,
                             uint8_t data_length);
bool usb_net_bridge_message_active(usb_board_channel_t channel);
bool usb_net_bridge_take_credit(usb_board_channel_t channel);
void usb_net_bridge_return_credit(usb_board_channel_t channel);
void usb_net_bridge_set_credit(usb_board_channel_t channel,
                               uint8_t credits);
uint8_t usb_net_bridge_credit(usb_board_channel_t channel);

#ifdef __cplusplus
}
#endif

#endif
