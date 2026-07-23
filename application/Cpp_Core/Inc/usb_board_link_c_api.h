#ifndef USB_BOARD_LINK_C_API_H
#define USB_BOARD_LINK_C_API_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usb_board_link_network_rx_callback_t)(const uint8_t *data,
                                                     uint16_t length);

bool UsbBoardLink_NetworkSend(const uint8_t *data, uint16_t length);
void UsbBoardLink_SetNetworkReceiveCallback(
    usb_board_link_network_rx_callback_t callback);
void UsbBoardLink_Process(void);

#ifdef __cplusplus
}
#endif

#endif
