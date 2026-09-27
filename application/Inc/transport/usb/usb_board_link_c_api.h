#ifndef USB_BOARD_LINK_C_API_H
#define USB_BOARD_LINK_C_API_H

#include <stdbool.h>
#include <stdint.h>
#include "webhid_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usb_board_link_network_rx_callback_t)(const uint8_t *data,
                                                     uint16_t length);
typedef bool (*usb_board_link_webconfig_rx_callback_t)(
    const uint8_t report[WEBHID_REPORT_BYTES]);

bool UsbBoardLink_NetworkSend(const uint8_t *data, uint16_t length);
void UsbBoardLink_SetNetworkReceiveCallback(
    usb_board_link_network_rx_callback_t callback);
bool UsbBoardLink_WebConfigSendReport(const uint8_t report[WEBHID_REPORT_BYTES]);
bool UsbBoardLink_WebConfigTakeFault(void);
void UsbBoardLink_WebConfigResetTransport(void);
void UsbBoardLink_SetWebConfigReceiveCallback(
    usb_board_link_webconfig_rx_callback_t callback);
void UsbBoardLink_Process(void);

#ifdef __cplusplus
}
#endif

#endif
