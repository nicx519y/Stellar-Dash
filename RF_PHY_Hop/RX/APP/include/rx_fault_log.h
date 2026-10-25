#ifndef XORA_RX_FAULT_LOG_H
#define XORA_RX_FAULT_LOG_H
#include <stdint.h>
#include "game_link_log.h"
void RX_LogInit(void);
void RX_LogClockRebase(void);
void RX_LogRadioReady(void);
void RX_LogPoll(void);
uint8_t RX_LogRequest(const uint8_t *request,uint16_t length);
void RX_LogUsbReset(void);
extern volatile uint8_t rx_log_paused;
static inline uint8_t RX_LogPaused(void) { return rx_log_paused; }
uint8_t RF_LogPause(uint8_t pause);
void RF_GameLogPoll(void);
#endif
