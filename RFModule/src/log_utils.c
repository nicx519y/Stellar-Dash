#include "log_utils.h"

#include "CH58x_common.h"

#include <string.h>

void log_uart_init(void)
{
    GPIOPinRemap(ENABLE, RB_PIN_UART0);
    GPIOA_SetBits(GPIO_Pin_14);
    /* UART0: TX PA14, RX PA15 */
    GPIOA_ModeCfg(GPIO_Pin_15, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_14, GPIO_ModeOut_PP_5mA);
    UART0_DefInit();
    UART0_BaudRateCfg(115200);
}

void log_raw(const char *s)
{
    UART0_SendString((uint8_t *)s, (uint16_t)strlen(s));
}

void log_hex8(uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char s[2];
    s[0] = hex[(v >> 4) & 0x0Fu];
    s[1] = hex[v & 0x0Fu];
    UART0_SendString((uint8_t *)s, 2u);
}
