#ifndef USB_BOARD_LINK_PORT_HPP
#define USB_BOARD_LINK_PORT_HPP

#include <stdint.h>

bool USBBoardLinkPort_Init();
bool USBBoardLinkPort_InitIap();
void USBBoardLinkPort_Shutdown();
bool USBBoardLinkPort_Send(const uint8_t *frame, uint8_t frameLength);
bool USBBoardLinkPort_Transact(const uint8_t *frame,
                               uint8_t frameLength,
                               uint8_t *response,
                               uint8_t responseCapacity,
                               uint8_t *responseLength,
                               uint32_t timeoutMs);
bool USBBoardLinkPort_HasEvent();
bool USBBoardLinkPort_ReadEvent(uint8_t *response,
                                uint8_t responseCapacity,
                                uint8_t *responseLength);
bool USBBoardLinkPort_RawTransact(const uint8_t *request,
                                  uint16_t requestLength,
                                  uint8_t *response,
                                  uint16_t responseLength,
                                  uint32_t timeoutMs);

#endif
