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
#include "dongle_config.h"
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
#ifndef RF_SERIAL_LOG
#define RF_SERIAL_LOG 0
#endif
#define RX_MAIN_TMR0_WRAP        0x04000000UL
#define RX_MAIN_LOG_PERIOD_TICKS (FREQ_SYS * 5u)
#define RX_MAIN_HID_TELEMETRY_PERIOD_MS 100u
#define RX_LED_PAIR_TOGGLE_MS    250u
#define RX_LED_COMM_TOGGLE_MS    1000u
/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#endif

void LED_Ctrl(uint8_t on);
void LED_Ctrl_Service(void);

#if (RF_SERIAL_LOG == 1)
static uint8_t s_main_pending_log = FALSE;
static char s_main_pending_msg[128];
#endif

static uint8_t RX_MainTmr0Elapsed(uint32_t now, uint32_t *last, uint32_t *acc, uint32_t period)
{
    uint32_t delta;

    if(now >= *last)
    {
        delta = now - *last;
    }
    else
    {
        delta = (RX_MAIN_TMR0_WRAP - *last) + now;
    }
    *last = now;
    *acc += delta;
    if(*acc < period)
    {
        return FALSE;
    }
    *acc -= period;
    return TRUE;
}

#if (RF_SERIAL_LOG == 1)
static uint8_t RX_MainSendCdc(const char *msg)
{
    uint16_t len;
    uint16_t max_len;

    if(USBHS_DevEnumStatus == 0)
    {
        return FALSE;
    }
    if((USBHS_Endp_Busy[DEF_UEP5] & DEF_UEP_BUSY) != 0)
    {
        return FALSE;
    }

    len = (uint16_t)strlen(msg);
#if defined(DONGLE_USB_FORCE_FULLSPEED) && (DONGLE_USB_FORCE_FULLSPEED != 0u)
    max_len = DEF_USB_EP5_FS_SIZE;
#else
    max_len = DEF_USB_EP5_HS_SIZE;
#endif
    if(len > max_len)
    {
        len = max_len;
    }
    if(USBHS_Endp_DataUp(DEF_UEP5, (uint8_t *)msg, len, DEF_UEP_CPY_LOAD) == 0u)
    {
        return TRUE;
    }

    return FALSE;
}
#endif

static void RX_MainFlushLog(void)
{
#if (RF_SERIAL_LOG == 1)
    if(s_main_pending_log == FALSE)
    {
        return;
    }

    if(RX_MainSendCdc(s_main_pending_msg) != FALSE)
    {
        s_main_pending_log = FALSE;
    }
#endif
}

static void RX_MainLog(const char *msg)
{
#if (RF_SERIAL_LOG == 1)
    strncpy(s_main_pending_msg, msg, sizeof(s_main_pending_msg) - 1u);
    s_main_pending_msg[sizeof(s_main_pending_msg) - 1u] = '\0';
    s_main_pending_log = TRUE;
    RX_MainFlushLog();
#else
    (void)msg;
#endif
}

static void RX_MainLogStats(void)
{
#if (RF_SERIAL_LOG == 1)
    char stats_msg[64];

    if(RF_GetStatsLine(stats_msg, sizeof(stats_msg)) == 0u)
    {
        return;
    }

    RX_MainLog(stats_msg);
#endif
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
    static uint32_t last_log_tmr = 0;
    static uint32_t log_acc_tmr = 0;
    static uint32_t last_hid_telemetry_clock = 0;
    static uint32_t rf_init_deadline = 0;
    static uint8_t rf_init_started = FALSE;
    static uint8_t rf_init_done = FALSE;
#if (RF_TEST_HEARTBEAT_LOG == 1)
    static uint32_t last_beat = 0;
#endif

    while(1)
    {
        uint32_t now = TMOS_GetSystemClock();
        uint32_t now_tmr = TMR0_GetCurrentTimer();
        LED_Ctrl_Service();
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
            RF_Init();
            rf_init_done = TRUE;
            RX_MainLog("R0\r\n");
            last_log_tmr = TMR0_GetCurrentTimer();
            log_acc_tmr = 0u;
            last_hid_telemetry_clock = TMOS_GetSystemClock();
            continue;
        }

        if((rf_init_done) &&
           ((uint32_t)(now - last_hid_telemetry_clock) >=
            MS1_TO_SYSTEM_TIME(RX_MAIN_HID_TELEMETRY_PERIOD_MS)))
        {
            last_hid_telemetry_clock = now;
            (void)RF_TrySendTelemetryReport();
        }

        if(RX_MainTmr0Elapsed(now_tmr,
                              &last_log_tmr,
                              &log_acc_tmr,
                              RX_MAIN_LOG_PERIOD_TICKS) != FALSE)
        {
            if(rf_init_done)
            {
                RX_MainLogStats();
            }
            else
            {
            }
        }

#if (RF_TEST_BYPASS_TMOS_AFTER_RF == 1) && (RF_TEST_HEARTBEAT_LOG == 1)
        if(rf_init_done)
        {
            if((uint32_t)(now - last_beat) >= MS1_TO_SYSTEM_TIME(5000u))
            {
                last_beat = now;
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
    static uint8_t led_output = FALSE;
    static uint8_t led_output_valid = FALSE;

    if(!led_inited)
    {
        GPIOA_ModeCfg(GPIO_Pin_10, GPIO_ModeOut_PP_5mA);
        GPIOA_SetBits(GPIO_Pin_10);
        led_inited = TRUE;
    }

    if((led_output_valid != FALSE) && (led_output == on))
    {
        return;
    }

    led_output = on;
    led_output_valid = TRUE;
    if(on)
    {
        GPIOA_ResetBits(GPIO_Pin_10);
    }
    else
    {
        GPIOA_SetBits(GPIO_Pin_10);
    }
}

static uint32_t LED_ToggleCyclesForMode(rf_indicator_mode_t mode)
{
    uint32_t ms;
    uint32_t cycles_per_ms;
    uint32_t cycles;

    switch(mode)
    {
    case RF_INDICATOR_BLINK_500MS:
        ms = RX_LED_PAIR_TOGGLE_MS;
        break;
    case RF_INDICATOR_BLINK_2000MS:
        ms = RX_LED_COMM_TOGGLE_MS;
        break;
    case RF_INDICATOR_OFF:
    case RF_INDICATOR_SOLID_ON:
    default:
        return 0u;
    }

    cycles_per_ms = GetSysClock() / 1000u;
    if(cycles_per_ms == 0u)
    {
        cycles_per_ms = 1u;
    }
    cycles = cycles_per_ms * ms;
    return (cycles == 0u) ? 1u : cycles;
}

static uint8_t LED_TmrElapsed(uint32_t now,
                              uint32_t *last,
                              uint32_t *acc,
                              uint32_t period)
{
    uint32_t delta;

    if(period == 0u)
    {
        return FALSE;
    }
    if(now >= *last)
    {
        delta = now - *last;
    }
    else
    {
        delta = (RX_MAIN_TMR0_WRAP - *last) + now;
    }

    *last = now;
    *acc += delta;
    if(*acc < period)
    {
        return FALSE;
    }

    *acc -= period;
    return TRUE;
}

static void LED_ApplyMode(rf_indicator_mode_t mode,
                          uint8_t *led_state,
                          uint32_t *toggle_cycles,
                          uint32_t *last_tmr,
                          uint32_t *acc_tmr)
{
    *toggle_cycles = LED_ToggleCyclesForMode(mode);
    *last_tmr = TMR0_GetCurrentTimer();
    *acc_tmr = 0u;

    switch(mode)
    {
    case RF_INDICATOR_SOLID_ON:
        *led_state = TRUE;
        LED_Ctrl(TRUE);
        break;
    case RF_INDICATOR_BLINK_500MS:
    case RF_INDICATOR_BLINK_2000MS:
        *led_state = TRUE;
        LED_Ctrl(TRUE);
        break;
    case RF_INDICATOR_OFF:
    default:
        *led_state = FALSE;
        LED_Ctrl(FALSE);
        break;
    }
}

/*********************************************************************
 * @fn      LED_Ctrl_Service
 *
 * @brief   状态指示灯模式服务，闪烁节拍轮询 TMR0 free-run 计数器
 *
 * @return  none
 */
void LED_Ctrl_Service(void)
{
    static rf_indicator_mode_t last_mode = RF_INDICATOR_OFF;
    static uint8_t service_started = FALSE;
    static uint8_t led_state = FALSE;
    static uint32_t toggle_cycles = 0u;
    static uint32_t last_tmr = 0u;
    static uint32_t acc_tmr = 0u;
    rf_indicator_mode_t mode = RF_GetIndicatorMode();
    uint32_t now_tmr;

    if((service_started == FALSE) || (mode != last_mode))
    {
        service_started = TRUE;
        last_mode = mode;
        LED_ApplyMode(mode,
                      &led_state,
                      &toggle_cycles,
                      &last_tmr,
                      &acc_tmr);
        return;
    }

    if((mode == RF_INDICATOR_OFF) ||
       (mode == RF_INDICATOR_SOLID_ON) ||
       (toggle_cycles == 0u))
    {
        return;
    }

    now_tmr = TMR0_GetCurrentTimer();
    if(LED_TmrElapsed(now_tmr, &last_tmr, &acc_tmr, toggle_cycles) != FALSE)
    {
        led_state = (led_state == FALSE) ? TRUE : FALSE;
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
    LED_Ctrl(TRUE);

    DelayMs(1000);

    LED_Ctrl(FALSE);

    CH58x_BLEInit();
    HAL_Init();
    RF_RoleInit();
    RF_USB_CompositeInit();
    Main_Circulation();
}

/******************************** endfile @ main ******************************/
