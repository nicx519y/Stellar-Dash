#include <assert.h>
#include <stdint.h>

#include "usb_board_link_port_ch585.h"

int main(void)
{
    usb_spi_rx_ring_t ring;
    uint16_t index;
    uint8_t byte;

    usb_spi_rx_ring_reset(&ring);

    /* One complete four-credit window of maximum 64-byte frames. */
    for(index = 0u; index < (4u * 64u); ++index)
    {
        assert(usb_spi_rx_ring_push(&ring, (uint8_t)index));
    }
    assert(ring.count == 256u);
    assert(ring.head == 256u);
    assert(ring.tail == 0u);
    for(index = 0u; index < (4u * 64u); ++index)
    {
        assert(usb_spi_rx_ring_pop(&ring, &byte));
        assert(byte == (uint8_t)index);
    }
    assert(ring.count == 0u);

    /* Fill, reject overflow without overwriting, then verify wrap ordering. */
    for(index = 0u; index < USB_SPI_RX_FIFO_BYTES; ++index)
    {
        assert(usb_spi_rx_ring_push(&ring, (uint8_t)(index ^ 0xA5u)));
    }
    assert(ring.count == USB_SPI_RX_FIFO_BYTES);
    assert(!usb_spi_rx_ring_push(&ring, 0xEEu));
    for(index = 0u; index < USB_SPI_RX_FIFO_BYTES; ++index)
    {
        assert(usb_spi_rx_ring_pop(&ring, &byte));
        assert(byte == (uint8_t)(index ^ 0xA5u));
    }
    assert(!usb_spi_rx_ring_pop(&ring, &byte));
    return 0;
}
