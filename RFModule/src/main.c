#include "platform_port.h"
#include "rfm_spi_bridge.h"
#include "log_utils.h"
#include "CH58x_common.h"

int main(void)
{
    uint32_t hb = 0u;
    uint8_t rst_flag;

    platform_clock_init();
    log_uart_init();
    rst_flag = (uint8_t)(R8_RESET_STATUS & RB_RESET_FLAG);
    log_raw("[RFM] rst=");
    log_hex8(rst_flag);
    log_raw("\r\n");
    WWDG_ResetCfg(DISABLE);
    WWDG_ITCfg(DISABLE);
    WWDG_ClearFlag();
    log_raw("[RFM] wdog off\r\n");
    log_raw("[RFM] clock ok\r\n");

    platform_gpio_init();
    log_raw("[RFM] gpio ok\r\n");

    platform_timer_init();
    log_raw("[RFM] timer ok\r\n");

    platform_spi_init();
    log_raw("[RFM] spi init ok\r\n");

    rfm_spi_bridge_init();
    log_raw("[RFM] spi bridge ready (loopback test)\r\n");

    while (1) {
        rfm_spi_bridge_poll();

        ++hb;
        if ((hb % 5000000u) == 0u) {
            log_raw("[RFM] hb\r\n");
        }
        platform_idle();
    }
}
