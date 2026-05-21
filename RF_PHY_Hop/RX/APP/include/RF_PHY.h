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

extern void RF_Init(void);
extern void RF_Service(void);
extern void RF_StartPacketLossScan(void);
extern void RF_StartQualityScoreScan(void);
extern uint8_t RF_IsQualityScoreScanActive(void);
extern uint8_t RF_HasPendingStatsLine(void);
extern uint16_t RF_GetStatsLine(char *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
