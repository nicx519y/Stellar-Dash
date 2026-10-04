#ifndef USB_BOARD_LINK_PORT_HPP
#define USB_BOARD_LINK_PORT_HPP

#include <stdint.h>

bool USBBoardLinkPort_Init();
bool USBBoardLinkPort_InitIap();
// Only after an explicit v1 DMA negotiation ACK; never inferred from timeout.
bool USBBoardLinkPort_EnableIapDma();
bool USBBoardLinkPort_InitApplication();
void USBBoardLinkPort_WaitApplicationReady();
bool USBBoardLinkPort_EnableFastApplication();
bool USBBoardLinkPort_DisableFastApplication();
bool USBBoardLinkPort_IsFastApplication();
uint32_t USBBoardLinkPort_ClockHz();
bool USBBoardLinkPort_EnableWebHid(uint32_t spiHz);
bool USBBoardLinkPort_SendWebHidBlock(const uint8_t *data, uint16_t length);
void USBBoardLink_HsAcceptBlock(const uint8_t *data, uint16_t length);
void USBBoardLink_HsTransportFault();
void USBBoardLinkPort_Shutdown();
bool USBBoardLinkPort_TryShutdown();
// RF wake only: one real SELECT_ROLE exchange with a total 20-ms budget.
bool USBBoardLinkPort_SelectRfRoleOnce();
bool USBBoardLinkPort_Send(const uint8_t *frame, uint8_t frameLength);
bool USBBoardLinkPort_LastMonitorTiming(uint32_t *start, uint32_t *end);
bool USBBoardLinkPort_Transact(const uint8_t *frame,
                               uint8_t frameLength,
                               uint8_t *response,
                               uint8_t responseCapacity,
                               uint8_t *responseLength,
                               uint32_t timeoutMs);
bool USBBoardLinkPort_HasEvent();
// Boot-ready uses the same low line, but is not a framed SPI response.
bool USBBoardLinkPort_RoleRequestSent();
// Sticky until shutdown/reacquire; independent of a received ACK's validity.
bool USBBoardLinkPort_HasReleaseFault();
// Synchronous control boundary only. A release edge permits progress; a
// latched fault never does. Runtime block/input polling stays non-blocking.
bool USBBoardLinkPort_WaitEventRelease(uint32_t timeoutMs);
bool USBBoardLinkPort_ReadEvent(uint8_t *response,
                                uint8_t responseCapacity,
                                uint8_t *responseLength);
bool USBBoardLinkPort_RawTransact(const uint8_t *request,
                                  uint16_t requestLength,
                                  uint8_t *response,
                                  uint16_t responseLength,
                                  uint32_t timeoutMs);
bool USBBoardLinkPort_RawDiscardPendingResponse(uint16_t responseLength,
                                                uint32_t timeoutMs);

#endif
