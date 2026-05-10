#include <stdbool.h>
#include <stdint.h>

#include "CH58x_common.h"
#include "HAL.h"
#include "dongle_config.h"
#include "log_utils.h"
#include "platform_port.h"
#include "usb_hid_if.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

int main(void)
{
    platform_clock_init();
    platform_gpio_init();

    platform_led_set(true);
    DelayMs(1000);
    platform_led_set(false);
    DelayMs(1000);

    // platform_timer_init();

    

    // platform_wdt_disable();


#ifdef DEBUG
    GPIOA_SetBits(GPIO_Pin_14);
    GPIOPinRemap(ENABLE, RB_PIN_UART0);
    GPIOA_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
    UART0_DefInit();
#endif

    PRINT("start.\r\n");
    PRINT("%s\r\n", VER_LIB);

    // platform_led_set(true);
    // DelayMs(1000);
    // platform_led_set(false);
    
    

    CH58x_BLEInit();

    platform_led_set(true);
    DelayMs(4000);
    platform_led_set(false);
    DelayMs(4000);

    HAL_Init();

    usb_hid_init();

    

    cdc_log_printf("[boot] USB full-speed XInput+CDC init\r\n");

    while (1) {
        platform_irq_ensure_enabled();
        TMOS_SystemProcess();
        usb_hid_poll();
    }
}
