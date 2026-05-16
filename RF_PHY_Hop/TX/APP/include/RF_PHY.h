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

#define SBP_RF_START_DEVICE_EVT      1
#define SBP_RF_PERIODIC_EVT          2
#define SBP_RF_RF_RX_EVT             4
#define SBP_RF_CHANNEL_HOP_TX_EVT    (1 << 3)
#define SBP_RF_CHANNEL_HOP_RX_EVT    (1 << 4)
#define SBP_RF_STAT_EVT              (1 << 5)

#define LLE_MODE_ORIGINAL_RX         (0x80) //�������LLEMODEʱ���ϴ˺꣬����յ�һ�ֽ�Ϊԭʼ���ݣ�ԭ��ΪRSSI��

extern void RF_Init(void);
extern void RF_TxMainLoopProcess(void);
extern bool RF_SPI_FastWriteInput(const uint8_t *payload, uint8_t len);
extern bool RF_SPI_FastWriteInput15(const uint8_t payload[15]);

#ifdef __cplusplus
}
#endif

#endif
