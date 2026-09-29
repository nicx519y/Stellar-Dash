#ifndef USB_BOARD_LINK_PORT_CH585_H
#define USB_BOARD_LINK_PORT_CH585_H

#include <stdbool.h>
#include <stdint.h>

#define USB_SPI_RX_FIFO_BYTES 8192u /* Two complete 3104-byte blocks plus control traffic. */
#define USB_SPI_RX_DMA_BYTES 4092u /* Aligned and within the 12-bit DMA transfer count. */
#define USB_SPI_TX_SLOTS 4u

/* Read the DMA wrap flag and cursor as one logical snapshot. A wrap between
 * acknowledging the flag and reading NOW requires a new cursor sample.
 * The SPI byte-count flag is deliberately not part of this interface. */
static inline uint32_t usb_spi_rx_dma_produced(
    volatile uint32_t *wrap_bytes, void *context,
    bool (*wrap_pending)(void *), void (*clear_wrap)(void *),
    uint32_t (*read_position)(void *))
{
    uint32_t position;
    do {
        if(wrap_pending(context)) {
            clear_wrap(context);
            *wrap_bytes += USB_SPI_RX_DMA_BYTES;
        }
        position = read_position(context);
    } while(wrap_pending(context));
    return *wrap_bytes + position;
}

/* CH585 DMA address registers expose the 17-bit SRAM bus offset, whereas
 * C pointers include the 0x20000000 CPU alias. Normalize both operands. */
static inline uint32_t usb_spi_rx_dma_position(uint32_t now,
                                                uint32_t buffer,
                                                uint32_t previous)
{
    const uint32_t begin = buffer & 0x1FFFFu;
    now &= 0x1FFFFu;
    if(now < begin || now > begin + USB_SPI_RX_DMA_BYTES) return previous;
    now -= begin;
    return now >= USB_SPI_RX_DMA_BYTES ? 0u : now;
}

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

static inline uint16_t usb_spi_rx_dma_delta(uint16_t previous,
                                             uint16_t current,
                                             bool loop_end)
{
    if(previous >= USB_SPI_RX_DMA_BYTES || current >= USB_SPI_RX_DMA_BYTES)
    {
        return 0u;
    }
    if((previous == current) && loop_end)
    {
        return USB_SPI_RX_DMA_BYTES;
    }
    if(current >= previous)
    {
        return (uint16_t)(current - previous);
    }
    return (uint16_t)((USB_SPI_RX_DMA_BYTES - previous) + current);
}

#define USB_SPI_STATIC_ASSERT_GLUE_(a, b) a##b
#define USB_SPI_STATIC_ASSERT_GLUE(a, b) USB_SPI_STATIC_ASSERT_GLUE_(a, b)
#define USB_SPI_STATIC_ASSERT(expr) \
    typedef char USB_SPI_STATIC_ASSERT_GLUE(usb_spi_static_assert_, __LINE__)[(expr) ? 1 : -1]

/*
 * Compatibility RX drains the eight-byte hardware FIFO by its half-full
 * interrupt. FAST_INPUT_V2 uses a 1024-byte circular DMA and settles DMA
 * progress into the same software ring before every RX->TX arbitration.
 */
/* V2 limits each ownership turn to one block and drains before handing back
 * NSS. Keep room for two maximum blocks plus all four control slots. */
USB_SPI_STATIC_ASSERT(USB_SPI_RX_FIFO_BYTES >= 2u * (32u + 3u * 1024u) + 4u * 64u);
USB_SPI_STATIC_ASSERT(USB_SPI_RX_FIFO_BYTES <= UINT16_MAX);
USB_SPI_STATIC_ASSERT(USB_SPI_RX_DMA_BYTES >= 64u);
USB_SPI_STATIC_ASSERT(USB_SPI_TX_SLOTS >= 4u);

void usb_board_link_port_spi_irq_handler(void);
void usb_board_link_port_nss_rise_irq_handler(void);

#endif
