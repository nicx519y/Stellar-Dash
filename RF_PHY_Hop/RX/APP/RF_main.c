/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2020/08/06
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/******************************************************************************/
/* ͷ�ļ����� */
#include "CONFIG.h"
#include "HAL.h"
#include "RF_PHY.h"
#include "ch585_usbhs_device.h"
#include <stdio.h>
#include <string.h>

void RF_USB_CompositeInit(void);

#ifndef RF_HOP_MODE
#define RF_HOP_MODE 2
#endif
#ifndef RF_TEST_BYPASS_TMOS_AFTER_RF
#define RF_TEST_BYPASS_TMOS_AFTER_RF 1
#endif
#ifndef RF_TEST_HEARTBEAT_LOG
#define RF_TEST_HEARTBEAT_LOG 0
#endif

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#endif

void LED_Ctrl(uint8_t on);
void LED_Ctrl_Blink(void);

static uint8_t s_main_pending_log = FALSE;
static char s_main_pending_msg[128];

static uint8_t RX_MainSendCdc(const char *msg)
{
    uint16_t len;

    if(USBHS_DevEnumStatus == 0)
    {
        return FALSE;
    }
    if((USBHS_Endp_Busy[DEF_UEP5] & DEF_UEP_BUSY) != 0)
    {
        return FALSE;
    }

    len = (uint16_t)strlen(msg);
    if(len > DEF_USB_EP5_HS_SIZE)
    {
        len = DEF_USB_EP5_HS_SIZE;
    }
    return (USBHS_Endp_DataUp(DEF_UEP5, (uint8_t *)msg, len, DEF_UEP_CPY_LOAD) == 0u) ? TRUE : FALSE;
}

static void RX_MainFlushLog(void)
{
    if(s_main_pending_log == FALSE)
    {
        return;
    }

    if(RX_MainSendCdc(s_main_pending_msg) != FALSE)
    {
        s_main_pending_log = FALSE;
    }
}

static void RX_MainLog(const char *msg)
{
    PRINT("%s", msg);
    strncpy(s_main_pending_msg, msg, sizeof(s_main_pending_msg) - 1u);
    s_main_pending_msg[sizeof(s_main_pending_msg) - 1u] = '\0';
    s_main_pending_log = TRUE;
    RX_MainFlushLog();
}

static void RX_MainLogRealtime(const char *msg)
{
    PRINT("%s", msg);
    (void)RX_MainSendCdc(msg);
}

/*********************************************************************
 * @fn      Main_Circulation
 *
 * @brief   ��ѭ��
 *
 * @return  none
 */
__HIGH_CODE
__attribute__((noinline))
void Main_Circulation()
{
    static uint32_t last_log = 0;
    static uint32_t rf_init_deadline = 0;
    static uint8_t rf_init_started = FALSE;
    static uint8_t rf_init_done = FALSE;
#if (RF_TEST_HEARTBEAT_LOG == 1)
    static uint32_t last_beat = 0;
#endif
    static char stats_msg[128];

    while(1)
    {
        uint32_t now = TMOS_GetSystemClock();
        LED_Ctrl_Blink();
#if (RF_TEST_BYPASS_TMOS_AFTER_RF == 1)
        if(rf_init_done)
        {
        }
        else
#endif
        {
        TMOS_SystemProcess();
        }
        RF_Service();
        RX_MainFlushLog();

        if(rf_init_deadline == 0u)
        {
            rf_init_deadline = now + MS1_TO_SYSTEM_TIME(3000u);
        }

        if((rf_init_started == FALSE) &&
           ((int32_t)(now - rf_init_deadline) >= 0))
        {
            rf_init_started = TRUE;
            RX_MainLog("[RX][MAIN] rf_init_begin\r\n");
            RF_Init();
            rf_init_done = TRUE;
            RX_MainLog("[RX][MAIN] rf_init_done\r\n");
        }

        if((uint32_t)(now - last_log) >= MS1_TO_SYSTEM_TIME(5000u))
        {
            last_log = now;
            if(rf_init_done)
            {
                if(RF_GetStatsLine(stats_msg, sizeof(stats_msg)) > 0u)
                {
                    RX_MainLogRealtime(stats_msg);
                }
            }
            else
            {
                RX_MainLogRealtime("[RX][MAIN] alive rf:0\r\n");
            }
        }

#if (RF_TEST_BYPASS_TMOS_AFTER_RF == 1) && (RF_TEST_HEARTBEAT_LOG == 1)
        if(rf_init_done)
        {
            if((uint32_t)(now - last_beat) >= MS1_TO_SYSTEM_TIME(5000u))
            {
                last_beat = now;
                RX_MainLog("[RX][MAIN] beat rf:1 bypass_tmos:1\r\n");
            }
        }
#endif
    }
}

/*********************************************************************
 * @fn      LED_Ctrl
 *
 * @brief   LED 控制，PA10 低电平点亮
 *
 * @param   on - 1: 点亮, 0: 熄灭
 *
 * @return  none
 */
void LED_Ctrl(uint8_t on)
{
    static uint8_t led_inited = FALSE;

    if(!led_inited)
    {
        GPIOA_ModeCfg(GPIO_Pin_10, GPIO_ModeOut_PP_5mA);
        GPIOA_SetBits(GPIO_Pin_10);
        led_inited = TRUE;
    }

    if(on)
    {
        GPIOA_ResetBits(GPIO_Pin_10);
    }
    else
    {
        GPIOA_SetBits(GPIO_Pin_10);
    }
}

/*********************************************************************
 * @fn      LED_Ctrl_Blink
 *
 * @brief   非阻塞闪烁，间隔 300ms 自动翻转
 *
 * @return  none
 */
void LED_Ctrl_Blink(void)
{
    static uint8_t  blink_started = FALSE;
    static uint8_t  led_state = FALSE;
    static uint32_t last_tick = 0;
    uint32_t now = TMOS_GetSystemClock();

    if(!blink_started)
    {
        blink_started = TRUE;
        led_state = FALSE;
        LED_Ctrl(led_state);
        last_tick = now;
        return;
    }

    if((uint32_t)(now - last_tick) >= 300)
    {
        last_tick = now;
        led_state = !led_state;
        LED_Ctrl(led_state);
    }
}

/*********************************************************************
 * @fn      main
 *
 * @brief   ������
 *
 * @return  none
 */
int main(void)
{
#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
    PWR_DCDCCfg(ENABLE);
#endif
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(SYSCLK_FREQ);
    LED_Ctrl(FALSE);
#if(defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
    GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
#endif
#ifdef DEBUG
    GPIOA_SetBits(GPIO_Pin_14);
    GPIOPinRemap(ENABLE, RB_PIN_UART0);
    GPIOA_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
    UART0_DefInit();
#endif
    PRINT("start.\n");
    PRINT("%s\n", VER_LIB);

    LED_Ctrl(TRUE);

    DelayMs(1000);

    LED_Ctrl(FALSE);

    CH58x_BLEInit();
    HAL_Init();
    RF_USB_CompositeInit();
    Main_Circulation();
}

/******************************** endfile @ main ******************************/
