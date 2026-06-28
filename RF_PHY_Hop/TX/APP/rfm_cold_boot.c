#include "rfm_cold_boot.h"

#include "rfm_spi_port_internal.h"

void rfm_cold_boot_signal_ready(void)
{
    /*
     * Cold boot READY uses the shared IRQ line before event traffic starts:
     * low means the CH584 SPI/RF application is initialized and the host may
     * send the first SET_RATE command. Later events still assert this line high.
     */
    rfm_spi_port_set_irq(false);
}
