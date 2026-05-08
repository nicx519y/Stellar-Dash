#include "UART.h"
#include "CH58x_common.h"
#include "log_utils.h"

void DebugInit(void)
{
    GPIOA_SetBits(GPIO_Pin_14);
    GPIOPinRemap(ENABLE, RB_PIN_UART0);
    GPIOA_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
    UART0_DefInit();
}

int main(void)
{
    uint32_t sec_counter = 0;

    SetSysClock(SYSCLK_FREQ);
    DebugInit();
    PRINT("Simulate CDC-HID Device running on USBHS Controller\r\n");

    USBHS_Device_Init(ENABLE);
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);

    while(1)
    {
        DelayMs(1000);
        sec_counter++;
        cdc_log_alive_tick(sec_counter);
    }
}
