#include "platform_port.h"
#include "rfm_link.h"
#include "rfm_spi_bridge.h"

int main(void)
{
    platform_clock_init();
    platform_gpio_init();
    platform_timer_init();
    platform_spi_init();

    rfm_link_init();
    rfm_spi_bridge_init();

    while (1) {
        rfm_link_poll();
        rfm_spi_bridge_poll();
        platform_idle();
    }
}
