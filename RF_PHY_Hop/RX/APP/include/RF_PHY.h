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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    RF_INDICATOR_OFF = 0u,
    RF_INDICATOR_SOLID_ON,
    /* Blink names describe the full on+off cycle. */
    RF_INDICATOR_BLINK_500MS,
    RF_INDICATOR_BLINK_2000MS
} rf_indicator_mode_t;

extern void RF_Init(void);
extern void RF_Service(void);
extern uint8_t RF_StartPairing(void);
extern uint8_t RF_StopPairing(void);
extern uint8_t RF_IsPairingActive(void);
extern rf_indicator_mode_t RF_GetIndicatorMode(void);
extern void RF_StartPacketLossScan(void);
extern void RF_StartQualityScoreScan(void);
extern uint8_t RF_IsQualityScoreScanActive(void);
extern uint8_t RF_HasPendingStatsLine(void);
extern uint16_t RF_GetStatsLine(char *buf, uint16_t len);
extern uint8_t RF_TrySendTelemetryReport(void);
extern uint16_t RF_GetTelemetryPeriodMs(void);
extern uint8_t RF_IsTelemetryEnabled(void);
extern uint8_t RF_IsRxSerialLogEnabled(void);
extern uint8_t RF_MonitorControlHandleReport(const uint8_t *report, uint16_t len);
extern void RF_MonitorControlFillReport(uint8_t *report, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
