#include <assert.h>
#include <stdint.h>

#include "usb_board_link_port_ch585.h"

int main(void)
{
    usb_spi_rx_ring_t ring;
    uint8_t value = 0u;
    uint16_t index;

    assert(usb_spi_rx_dma_delta(0u, 0u, false) == 0u);
    assert(usb_spi_rx_dma_delta(0u, 64u, false) == 64u);
    assert(usb_spi_rx_dma_delta(1010u, 10u, true) == 24u);
    assert(usb_spi_rx_dma_delta(17u, 17u, true) ==
           USB_SPI_RX_DMA_BYTES);

    usb_spi_rx_ring_reset(&ring);
    for(index = 0u; index < 128u; ++index)
    {
        assert(usb_spi_rx_ring_push(&ring, (uint8_t)index));
    }
    for(index = 0u; index < 64u; ++index)
    {
        assert(usb_spi_rx_ring_pop(&ring, &value));
        assert(value == (uint8_t)index);
    }
    for(index = 128u; index < 256u; ++index)
    {
        assert(usb_spi_rx_ring_push(&ring, (uint8_t)index));
    }
    for(index = 64u; index < 256u; ++index)
    {
        assert(usb_spi_rx_ring_pop(&ring, &value));
        assert(value == (uint8_t)index);
    }
    assert(!usb_spi_rx_ring_pop(&ring, &value));
    return 0;
}
