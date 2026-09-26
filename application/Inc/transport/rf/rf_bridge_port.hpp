#ifndef RF_BRIDGE_PORT_HPP
#define RF_BRIDGE_PORT_HPP

#include <stdint.h>

// Stable SPI bridge port API (STM32 side).
// Upper-layer RF transport should only use this API.
bool RFBridgePort_Transfer(const uint8_t* tx, uint16_t txLen, uint8_t* rx, uint16_t* rxLen);
bool RFBridgePort_ControlTransfer(const uint8_t* tx, uint16_t txLen, uint8_t* rx, uint16_t* rxLen);
bool RFBridgePort_ControlTransferWithTimeout(const uint8_t* tx,
                                             uint16_t txLen,
                                             uint8_t* rx,
                                             uint16_t* rxLen,
                                             uint32_t ackTimeoutMs);
bool RFBridgePort_ControlTransferForceTxWithTimeout(const uint8_t* tx,
                                                    uint16_t txLen,
                                                    uint8_t* rx,
                                                    uint16_t* rxLen,
                                                    uint32_t ackTimeoutMs);
bool RFBridgePort_SendNoResponse(const uint8_t* tx, uint16_t txLen);
bool RFBridgePort_SendInputLatest(const uint8_t* tx, uint16_t txLen);
bool RFBridgePort_PrepareWakeLineIdle(void);
bool RFBridgePort_WakePulse(void);
bool RFBridgePort_IsReady(void);
bool RFBridgePort_HasPendingEvent(void);
bool RFBridgePort_IsInputIdle(void);
bool RFBridgePort_ReadEvent(uint8_t* rx, uint16_t* rxLen);
void RFBridgePort_SetDmaReplyCapable(bool enabled);
bool RFBridgePort_DmaReplyCapable();
// DWT capture when the most recent validated event header was received.
uint32_t RFBridgePort_EventReceivedCycles();
void RFBridgePort_Shutdown(void);
// Sleep-only checked shutdown. On failure keep the peer powered and retry.
bool RFBridgePort_TryShutdownForSleep();
bool RFBridgePort_RecoveryBegin();
bool RFBridgePort_RecoveryIdle();
enum class RFPortStep { Pending, Complete, Error };
enum class RFRecoveryReadError : uint32_t {
    None, InvalidState, ReleaseTimeout, FrameTimeout, SpiTransfer,
    FrameLength, HeaderMissing, Checksum
};
// Last failed physical read, preserved across cleanup/retry. RAM only.
struct RFRecoveryReadDiagnostic {
    uint32_t failures, reason, atMs, rawBytes, frameOffset, frameBytes, irqAsserted, spiError;
    uint8_t raw[64];
};
extern volatile RFRecoveryReadDiagnostic g_rfRecoveryReadDiagnostic;
// Exclusive cold-recovery I/O; never waits for IRQ release or DMA completion.
bool RFBridgePort_RecoverySend(const uint8_t* tx, uint16_t len);
RFPortStep RFBridgePort_RecoveryRead(uint8_t* rx, uint16_t* len, uint32_t now);
void RFBridgePort_CancelRecoveryIo();

#endif

bool RFBridgePort_LastInputTiming(uint32_t* start,uint32_t* end);
