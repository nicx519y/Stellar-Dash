#include "UART.h"
#include "CH58x_common.h"
#include "HAL.h"
#include "dongle_fsm.h"
#include "platform_port.h"
#include "rf_link.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

typedef enum {
    APP_STATE_WAIT_PAIR = 0,
    APP_STATE_WAIT_CONNECT,
    APP_STATE_SWITCH_TO_USB,
    APP_STATE_USB_ACTIVE
} app_state_t;

void DebugInit(void)
{
    GPIOA_SetBits(GPIO_Pin_14);
    GPIOPinRemap(ENABLE, RB_PIN_UART0);
    GPIOA_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
    UART0_DefInit();
}

static void usb_start_once(void)
{
    USBHS_Device_Init(ENABLE);
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);
    PFIC_EnableAllIRQ();
    PRINT("USBHS started (XInput+CDC)\r\n");
}

static void pair_fsm_stop_before_usb(void)
{
    /* Stop pairing/connecting requests before USB starts; keep RF data path alive. */
    rf_link_stop_pairing();
    rf_link_stop_connect();
    PRINT("Pair/connect FSM paused before USB start.\r\n");
}

int main(void)
{
    uint32_t now_us;
    uint8_t rf_role_status;
    app_state_t app_state = APP_STATE_WAIT_PAIR;
    app_state_t last_logged_state = (app_state_t)0xFF;
    dongle_state_t fsm_state;

    SetSysClock(SYSCLK_FREQ);
    DebugInit();
    PRINT("Dongle start: BLE/RF first, USB after CONNECTED\r\n");

    platform_gpio_init();
    platform_timer_init();

    CH58x_BLEInit();
    HAL_Init();
    rf_role_status = RF_RoleInit();
    PRINT("RF_RoleInit status=%u\r\n", (unsigned int)rf_role_status);

    rf_link_init(0);
    dongle_fsm_init(platform_now_us());

    while(1)
    {
        switch (app_state) {
        case APP_STATE_WAIT_PAIR:
        case APP_STATE_WAIT_CONNECT:
            platform_irq_ensure_enabled();
            TMOS_SystemProcess();
            now_us = platform_now_us();

            rf_link_poll();
            dongle_fsm_tick(now_us);

            fsm_state = dongle_fsm_get_state();
            if (fsm_state == DONGLE_STATE_CONNECTED) {
                app_state = APP_STATE_SWITCH_TO_USB;
            } else if ((fsm_state == DONGLE_STATE_CONNECTING) || (fsm_state == DONGLE_STATE_PAIRED_OK)) {
                app_state = APP_STATE_WAIT_CONNECT;
            } else {
                app_state = APP_STATE_WAIT_PAIR;
            }
            break;

        case APP_STATE_SWITCH_TO_USB:
            pair_fsm_stop_before_usb();
            usb_start_once();
            PRINT("RF connected, USB enum enabled.\r\n");
            app_state = APP_STATE_USB_ACTIVE;
            break;

        case APP_STATE_USB_ACTIVE:
            /* Keep RF/FSM alive so link-lost can transition back to waiting/connect states. */
            rf_link_poll();
            now_us = platform_now_us();
            dongle_fsm_tick(now_us);
            fsm_state = dongle_fsm_get_state();
            if (fsm_state == DONGLE_STATE_CONNECTED) {
                /* Stay in USB active mode. */
            } else if ((fsm_state == DONGLE_STATE_CONNECTING) || (fsm_state == DONGLE_STATE_PAIRED_OK)) {
                app_state = APP_STATE_WAIT_CONNECT;
            } else {
                app_state = APP_STATE_WAIT_PAIR;
            }
            platform_idle();
            break;

        default:
            app_state = APP_STATE_WAIT_PAIR;
            break;
        }

        if (app_state != last_logged_state) {
            last_logged_state = app_state;
            if (app_state == APP_STATE_WAIT_PAIR) {
                PRINT("APP_STATE_WAIT_PAIR\r\n");
            } else if (app_state == APP_STATE_WAIT_CONNECT) {
                PRINT("APP_STATE_WAIT_CONNECT\r\n");
            } else if (app_state == APP_STATE_SWITCH_TO_USB) {
                PRINT("APP_STATE_SWITCH_TO_USB\r\n");
            } else if (app_state == APP_STATE_USB_ACTIVE) {
                PRINT("APP_STATE_USB_ACTIVE\r\n");
            }
        }
    }
}
