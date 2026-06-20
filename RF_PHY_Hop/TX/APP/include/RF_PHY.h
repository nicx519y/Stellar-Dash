/********************************** (C) COPYRIGHT *******************************
 * File Name          : rf_phy.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/11/12
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#ifndef CENTRAL_H
#define CENTRAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RF_LINK_STATE_IDLE = 0u,
    RF_LINK_STATE_PAIRING = 1u,
    RF_LINK_STATE_PAIR_OK = 2u,
    RF_LINK_STATE_CONNECTING = 3u,
    RF_LINK_STATE_CONNECTED = 4u,
    RF_LINK_STATE_RECONNECTING = 5u,
    RF_LINK_STATE_PAIR_TIMEOUT = 6u,
    RF_LINK_STATE_PAIR_FAILED = 7u
} rf_link_state_code_t;

extern void RF_Init(void);
extern void RF_TxMainLoopProcess(void);
extern bool RF_SPI_FastWriteInput(const uint8_t *payload, uint8_t len);
extern bool RF_SetReportRateHz(uint16_t hz);
extern uint16_t RF_GetReportRateHz(void);
extern bool RF_StartPairing(void);
extern bool RF_StopPairing(void);
extern bool RF_Unbind(void);
extern uint8_t RF_GetLinkStateCode(void);
extern uint8_t RF_ConsumePendingEventStateCode(void);
extern uint8_t RF_PeekPendingEventStateCode(void);
extern void RF_ClearPendingEventStateCode(uint8_t state_code);
extern uint8_t RF_IsConnected(void);
extern uint8_t RF_HasBond(void);
extern uint16_t RF_GetRxOkCount(void);
extern uint16_t RF_GetRxFailCount(void);
extern uint16_t RF_GetTxFailCount(void);
extern uint32_t RF_GetRejectCount(void);

#ifdef __cplusplus
}
#endif

#endif
