#include "rfm_spi_port_internal.h"

#include <string.h>

#include "CH58x_common.h"

#define SPI_PINS                      (GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15)
#define SPI_IRQ_PIN                   (GPIO_Pin_11)
#define SPI_TX_PENDING_RECOVER_US     (50000u)
#define US_TICK_STEP                  (10u)
#define SPI_RX_DMA_RING_SIZE          (65536u)
#define SPI_RX_TOTAL_CNT              (0u)
#define SPI_RX_BACKLOG_DROP_THRESHOLD (4096u)
#define SPI_RX_BACKLOG_KEEP_BYTES     (19u * 64u)

static uint8_t s_spi_tx_buf[96];
static uint16_t s_spi_tx_len;
static uint16_t s_spi_tx_pos;
static uint8_t s_spi_tx_pending;
static uint32_t s_spi_tx_start_us;
static volatile uint32_t s_now_us;

__attribute__((aligned(4))) static uint8_t s_spi_rx_dma_buf[SPI_RX_DMA_RING_SIZE];
static volatile uint32_t s_spi_rx_dma_wrap_count;
static uint32_t s_spi_rx_base_abs;
static uint32_t s_spi_rx_read_abs;
static uint32_t s_spi_rx_seen_wrap_count;
static volatile uint32_t s_spi_rx_total_bytes;
static volatile uint32_t s_spi_rx_ring_overrun_count;
static volatile uint32_t s_spi_rx_backlog_drop_count;
static volatile uint32_t s_spi_rx_backlog_drop_bytes;
static volatile uint32_t s_spi_rx_max_available;
static volatile uint32_t s_spi_rx_near_full_count;
static volatile uint32_t s_spi_rx_full_clip_count;
static volatile uint32_t s_spi_rx_isr_count;
static volatile uint32_t s_spi_rx_fifo_ov_count;
static volatile uint32_t s_spi_rx_bad_irq_count;
static volatile uint32_t s_spi_rx_last_flags;

static void spi_rx_dma_loop_start(uint8_t flush_fifo)
{
    R8_SPI0_CTRL_CFG &= (uint8_t)(~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP));
    R8_SPI0_CTRL_MOD = (uint8_t)((R8_SPI0_CTRL_MOD | RB_SPI_FIFO_DIR) & (uint8_t)(~RB_SPI_SLV_CMD_MOD));
    if (flush_fifo != 0u) {
        while (R8_SPI0_FIFO_COUNT != 0u) {
            (void)R8_SPI0_FIFO;
        }
    }

    R32_SPI0_DMA_BEG = (uint32_t)s_spi_rx_dma_buf;
    R32_SPI0_DMA_END = (uint32_t)(s_spi_rx_dma_buf + SPI_RX_DMA_RING_SIZE);
    R32_SPI0_DMA_NOW = (uint32_t)s_spi_rx_dma_buf;
    R16_SPI0_TOTAL_CNT = SPI_RX_TOTAL_CNT;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END | RB_SPI_IF_FIFO_OV |
                       RB_SPI_IF_FIFO_HF | RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE;
    R8_SPI0_CTRL_CFG |= (uint8_t)(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
}

static uint32_t spi_rx_dma_pos(void)
{
    uint32_t now = R32_SPI0_DMA_NOW;
    uint32_t beg = (uint32_t)s_spi_rx_dma_buf;
    uint32_t end = (uint32_t)(s_spi_rx_dma_buf + SPI_RX_DMA_RING_SIZE);

    if ((now < beg) || (now > end)) {
        return 0u;
    }
    now -= beg;
    if (now >= SPI_RX_DMA_RING_SIZE) {
        now = 0u;
    }
    return now;
}

static void spi_rx_restart_after_tx(void)
{
    PFIC_DisableIRQ(SPI0_IRQn);
    s_spi_rx_base_abs = s_spi_rx_total_bytes;
    s_spi_rx_dma_wrap_count = 0u;
    s_spi_rx_seen_wrap_count = 0u;
    s_spi_rx_read_abs = s_spi_rx_base_abs;
    spi_rx_dma_loop_start(1u);
    SPI0_ITCfg(ENABLE, SPI0_IT_DMA_END | SPI0_IT_FIFO_OV);
    PFIC_EnableIRQ(SPI0_IRQn);
}

static void spi_tx_fill_fifo(void)
{
    while ((s_spi_tx_pos < s_spi_tx_len) && (R8_SPI0_FIFO_COUNT < SPI_FIFO_SIZE)) {
        R8_SPI0_FIFO = s_spi_tx_buf[s_spi_tx_pos++];
    }
}

static uint32_t spi_now_us(void) { s_now_us += US_TICK_STEP; return s_now_us; }

void rfm_spi_port_init(void)
{
    GPIOPinRemap(ENABLE, RB_PIN_SPI0);
    GPIOADigitalCfg(ENABLE, (uint16_t)0xFFFFu);
    GPIOBDigitalCfg(ENABLE, SPI_PINS | SPI_IRQ_PIN);
    GPIOB_ModeCfg(SPI_IRQ_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(SPI_IRQ_PIN);

    GPIOB_ModeCfg(GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeOut_PP_5mA);

    SPI0_SlaveInit();
    R8_SPI0_CTRL_MOD = (uint8_t)((R8_SPI0_CTRL_MOD | RB_SPI_FIFO_DIR) & (uint8_t)(~RB_SPI_SLV_CMD_MOD));

    s_spi_tx_pending = 0u;
    s_spi_tx_start_us = 0u;
    s_spi_tx_len = 0u;
    s_spi_tx_pos = 0u;
    s_now_us = 0u;
    s_spi_rx_dma_wrap_count = 0u;
    s_spi_rx_base_abs = 0u;
    s_spi_rx_seen_wrap_count = 0u;
    s_spi_rx_read_abs = 0u;
    s_spi_rx_total_bytes = 0u;
    s_spi_rx_ring_overrun_count = 0u;
    s_spi_rx_backlog_drop_count = 0u;
    s_spi_rx_backlog_drop_bytes = 0u;
    s_spi_rx_max_available = 0u;
    s_spi_rx_near_full_count = 0u;
    s_spi_rx_full_clip_count = 0u;
    s_spi_rx_isr_count = 0u;
    s_spi_rx_fifo_ov_count = 0u;
    s_spi_rx_bad_irq_count = 0u;
    s_spi_rx_last_flags = 0u;

    spi_rx_dma_loop_start(1u);
    SPI0_ITCfg(ENABLE, SPI0_IT_DMA_END | SPI0_IT_FIFO_OV);
    PFIC_EnableIRQ(SPI0_IRQn);
}

void rfm_spi_port_set_irq(bool asserted)
{
    if (asserted) {
        GPIOB_SetBits(SPI_IRQ_PIN);
    } else {
        GPIOB_ResetBits(SPI_IRQ_PIN);
    }
}

size_t rfm_spi_port_drain(uint8_t *buf, size_t max_len)
{
    size_t n = 0u;
    uint32_t wraps;
    uint32_t write_pos;
    uint32_t write_local_abs;
    uint32_t write_abs;
    uint32_t available;
    uint32_t read_pos;
    uint32_t read_local_abs;
    uint32_t chunk;

    if ((buf == 0) || (max_len == 0u)) {
        return 0u;
    }

    if (s_spi_tx_pending != 0u) {
        spi_tx_fill_fifo();
        if ((R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END) == 0u) {
            if (GPIOB_ReadPortPin(GPIO_Pin_12) &&
                ((int32_t)(spi_now_us() - (s_spi_tx_start_us + SPI_TX_PENDING_RECOVER_US)) >= 0)) {
                s_spi_tx_pending = 0u;
                rfm_spi_port_set_irq(false);
                spi_rx_restart_after_tx();
            } else {
                return 0u;
            }
        } else {
            R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
            s_spi_tx_pending = 0u;
            rfm_spi_port_set_irq(false);
            spi_rx_restart_after_tx();
        }
    }

    PFIC_DisableIRQ(SPI0_IRQn);
    wraps = s_spi_rx_dma_wrap_count;
    write_pos = spi_rx_dma_pos();
    PFIC_EnableIRQ(SPI0_IRQn);

    read_local_abs = s_spi_rx_read_abs - s_spi_rx_base_abs;
    if ((write_pos < (read_local_abs % SPI_RX_DMA_RING_SIZE)) &&
        (wraps == s_spi_rx_seen_wrap_count)) {
        wraps++;
    }

    write_local_abs = (wraps * SPI_RX_DMA_RING_SIZE) + write_pos;
    write_abs = s_spi_rx_base_abs + write_local_abs;
    if (write_abs < s_spi_rx_read_abs) {
        s_spi_rx_read_abs = write_abs;
    }

    available = write_abs - s_spi_rx_read_abs;
    if (available > SPI_RX_DMA_RING_SIZE) {
        s_spi_rx_ring_overrun_count += available - SPI_RX_DMA_RING_SIZE;
        s_spi_rx_full_clip_count++;
        s_spi_rx_read_abs = write_abs - SPI_RX_DMA_RING_SIZE;
        available = SPI_RX_DMA_RING_SIZE;
    } else if (available > s_spi_rx_max_available) {
        s_spi_rx_max_available = available;
    }

    if (available > SPI_RX_BACKLOG_DROP_THRESHOLD) {
        uint32_t keep = SPI_RX_BACKLOG_KEEP_BYTES;
        uint32_t drop;

        if (keep > max_len) {
            keep = (uint32_t)max_len;
        }
        if (keep > available) {
            keep = available;
        }

        drop = available - keep;
        s_spi_rx_read_abs += drop;
        s_spi_rx_backlog_drop_count++;
        s_spi_rx_backlog_drop_bytes += drop;
        available = keep;
    }

    if (available > (SPI_RX_DMA_RING_SIZE - 4096u)) {
        s_spi_rx_near_full_count++;
    }
    if (available > max_len) {
        available = (uint32_t)max_len;
    }

    while (n < available) {
        read_pos = (s_spi_rx_read_abs - s_spi_rx_base_abs) % SPI_RX_DMA_RING_SIZE;
        chunk = SPI_RX_DMA_RING_SIZE - read_pos;
        if (chunk > (available - (uint32_t)n)) {
            chunk = available - (uint32_t)n;
        }
        memcpy(&buf[n], &s_spi_rx_dma_buf[read_pos], chunk);
        n += chunk;
        s_spi_rx_read_abs += chunk;
    }

    s_spi_rx_seen_wrap_count = (s_spi_rx_read_abs - s_spi_rx_base_abs) / SPI_RX_DMA_RING_SIZE;
    s_spi_rx_total_bytes = write_abs;

    return n;
}

uint32_t rfm_spi_port_rx_ring_overrun_count(void)
{
    return s_spi_rx_ring_overrun_count;
}

uint32_t rfm_spi_port_rx_backlog_drop_count(void)
{
    return s_spi_rx_backlog_drop_count;
}

uint32_t rfm_spi_port_rx_backlog_drop_bytes(void)
{
    return s_spi_rx_backlog_drop_bytes;
}

uint32_t rfm_spi_port_rx_byte_count(void)
{
    return s_spi_rx_total_bytes;
}

uint32_t rfm_spi_port_rx_dma_pos(void)
{
    return spi_rx_dma_pos();
}

uint32_t rfm_spi_port_rx_fifo_ov_count(void)
{
    return s_spi_rx_fifo_ov_count;
}

uint32_t rfm_spi_port_rx_max_available(void)
{
    return s_spi_rx_max_available;
}

uint32_t rfm_spi_port_rx_take_max_available(void)
{
    uint32_t max_value;

    PFIC_DisableIRQ(SPI0_IRQn);
    max_value = s_spi_rx_max_available;
    s_spi_rx_max_available = 0u;
    PFIC_EnableIRQ(SPI0_IRQn);

    return max_value;
}

uint32_t rfm_spi_port_rx_take_near_full_count(void)
{
    uint32_t count;

    PFIC_DisableIRQ(SPI0_IRQn);
    count = s_spi_rx_near_full_count;
    s_spi_rx_near_full_count = 0u;
    PFIC_EnableIRQ(SPI0_IRQn);

    return count;
}

uint32_t rfm_spi_port_rx_take_full_clip_count(void)
{
    uint32_t count;

    PFIC_DisableIRQ(SPI0_IRQn);
    count = s_spi_rx_full_clip_count;
    s_spi_rx_full_clip_count = 0u;
    PFIC_EnableIRQ(SPI0_IRQn);

    return count;
}

uint32_t rfm_spi_port_rx_bad_irq_count(void)
{
    return s_spi_rx_bad_irq_count;
}

uint32_t rfm_spi_port_rx_isr_count(void)
{
    return s_spi_rx_isr_count;
}

uint32_t rfm_spi_port_rx_last_flags(void)
{
    return s_spi_rx_last_flags;
}

uint32_t rfm_spi_port_rx_direct_count(void)
{
    return 0u;
}

bool rfm_spi_port_try_read(uint8_t *buf, size_t *inout_len)
{
    size_t n;

    if ((buf == 0) || (inout_len == 0) || (*inout_len == 0u)) {
        return false;
    }

    n = rfm_spi_port_drain(buf, *inout_len);
    if (n == 0u) {
        return false;
    }
    *inout_len = n;
    return true;
}

__attribute__((interrupt("WCH-Interrupt-fast"), section(".highcode")))
void SPI0_IRQHandler(void)
{
    const uint8_t flags = R8_SPI0_INT_FLAG;
    const uint8_t active = (uint8_t)(flags & (RB_SPI_IF_DMA_END | RB_SPI_IF_FIFO_OV));
    const uint8_t noise = (uint8_t)(flags & (RB_SPI_IF_BYTE_END | RB_SPI_IF_FIFO_HF | RB_SPI_IF_FST_BYTE));

    s_spi_rx_last_flags = flags;

    if (flags == 0u) {
        return;
    }

    if (noise != 0u) {
        R8_SPI0_INT_FLAG = noise;
        if (active == 0u) {
            return;
        }
    }

    if ((active & RB_SPI_IF_FIFO_OV) != 0u) {
        R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
        s_spi_rx_fifo_ov_count++;
        s_spi_rx_ring_overrun_count++;
        spi_rx_dma_loop_start(1u);
        return;
    }

    if ((active & RB_SPI_IF_DMA_END) != 0u) {
        R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END;
        s_spi_rx_isr_count++;
        s_spi_rx_dma_wrap_count++;
        return;
    }

    if (flags != 0u) {
        R8_SPI0_INT_FLAG = flags;
    }
    s_spi_rx_bad_irq_count++;
}

bool rfm_spi_port_try_write(const uint8_t *buf, size_t len)
{
    size_t i;

    if ((buf == 0) || (len == 0u) || (len > 4095u) || (len > sizeof(s_spi_tx_buf))) {
        return false;
    }

    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_OV);
    PFIC_DisableIRQ(SPI0_IRQn);
    R8_SPI0_CTRL_CFG &= (uint8_t)(~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP));
    R8_SPI0_CTRL_MOD &= (uint8_t)(~RB_SPI_FIFO_DIR);
    R16_SPI0_TOTAL_CNT = (uint16_t)len;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;

    while (R8_SPI0_FIFO_COUNT != 0u) {
        (void)R8_SPI0_FIFO;
    }

    for (i = 0u; i < len; ++i) {
        s_spi_tx_buf[i] = buf[i];
    }
    s_spi_tx_len = (uint16_t)len;
    s_spi_tx_pos = 0u;
    spi_tx_fill_fifo();
    s_spi_tx_pending = 1u;
    s_spi_tx_start_us = spi_now_us();
    return true;
}
