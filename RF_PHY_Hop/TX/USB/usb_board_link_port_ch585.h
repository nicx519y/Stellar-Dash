#ifndef USB_BOARD_LINK_PORT_CH585_H
#define USB_BOARD_LINK_PORT_CH585_H

#include <stdbool.h>
#include <stdint.h>

#define USB_SPI_RX_DMA_BYTES 64u
#define USB_SPI_RX_FIFO_BYTES 16384u
#define USB_SPI_TX_SLOTS 4u

typedef struct
{
    uint8_t data[USB_SPI_RX_FIFO_BYTES];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} usb_spi_rx_ring_t;

static inline void usb_spi_rx_ring_reset(usb_spi_rx_ring_t *ring)
{
    ring->head = 0u;
    ring->tail = 0u;
    ring->count = 0u;
}

static inline bool usb_spi_rx_ring_push(usb_spi_rx_ring_t *ring, uint8_t byte)
{
    if(ring->count >= USB_SPI_RX_FIFO_BYTES)
    {
        return false;
    }
    ring->data[ring->head] = byte;
    ++ring->head;
    if(ring->head >= USB_SPI_RX_FIFO_BYTES)
    {
        ring->head = 0u;
    }
    ++ring->count;
    return true;
}

static inline bool usb_spi_rx_ring_pop(usb_spi_rx_ring_t *ring, uint8_t *byte)
{
    if((byte == 0) || (ring->count == 0u))
    {
        return false;
    }
    *byte = ring->data[ring->tail];
    ++ring->tail;
    if(ring->tail >= USB_SPI_RX_FIFO_BYTES)
    {
        ring->tail = 0u;
    }
    --ring->count;
    return true;
}

#define USB_SPI_STATIC_ASSERT_GLUE_(a, b) a##b
#define USB_SPI_STATIC_ASSERT_GLUE(a, b) USB_SPI_STATIC_ASSERT_GLUE_(a, b)
#define USB_SPI_STATIC_ASSERT(expr) \
    typedef char USB_SPI_STATIC_ASSERT_GLUE(usb_spi_static_assert_, __LINE__)[(expr) ? 1 : -1]

/*
 * R16_SPI0_TOTAL_CNT implements only 12 bits.  UsbBoardLink transactions are
 * at most 64 bytes, so RX uses one 64-byte DMA buffer per NSS assertion.
 * The PA12 rising-edge ISR moves that complete transaction into the 16-KiB
 * software ring and only then rearms DMA while NSS is high.  DMA is therefore
 * never reloaded in the middle of an active transaction.
 */
USB_SPI_STATIC_ASSERT(USB_SPI_RX_DMA_BYTES > 0u);
USB_SPI_STATIC_ASSERT(USB_SPI_RX_DMA_BYTES <= 0x0FFFu);
USB_SPI_STATIC_ASSERT(USB_SPI_RX_DMA_BYTES == 64u);
USB_SPI_STATIC_ASSERT(USB_SPI_RX_FIFO_BYTES >= 16384u);
USB_SPI_STATIC_ASSERT(USB_SPI_RX_DMA_BYTES <= UINT16_MAX);
USB_SPI_STATIC_ASSERT(USB_SPI_RX_FIFO_BYTES <= UINT16_MAX);
USB_SPI_STATIC_ASSERT(USB_SPI_TX_SLOTS >= 4u);

void usb_board_link_port_spi_irq_handler(void);
void usb_board_link_port_nss_rise_irq_handler(void);

#endif
