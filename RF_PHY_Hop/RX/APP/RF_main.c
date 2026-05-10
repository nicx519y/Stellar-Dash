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

void RF_USB_CompositeInit(void);

#ifndef RF_HOP_MODE
#define RF_HOP_MODE 2
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
    while(1)
    {
        LED_Ctrl_Blink();
        TMOS_SystemProcess();
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
    RF_SetRoleInitStatus(RF_RoleInit());
    RF_Init();
    RF_USB_CompositeInit();
    Main_Circulation();
}

/******************************** endfile @ main ******************************/
