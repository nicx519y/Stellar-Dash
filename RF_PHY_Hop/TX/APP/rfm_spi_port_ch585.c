#include "rfm_spi_port_internal.h"

#include <string.h>

#include "CH58x_common.h"
#include "rfm_config.h"

#define SPI_PINS                      (GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15)
#define SPI_IRQ_PIN                   (GPIO_Pin_11)
#define SPI_TX_PENDING_RECOVER_US     (50000u)
#define US_TICK_STEP                  (10u)
#define SPI_RX_FRAME_BYTES            (3u + RFM_RF_INPUT_PAYLOAD_LEN + 1u)
#define SPI_RX_DMA_RING_FRAMES        (128u)
#define SPI_RX_DMA_RING_SIZE          (SPI_RX_FRAME_BYTES * SPI_RX_DMA_RING_FRAMES)
#define SPI_RX_TOTAL_CNT              (SPI_RX_DMA_RING_SIZE)
#define SPI_RX_PEEK_SCAN_FRAMES       (3u)
#define SPI_RX_PEEK_SCAN_BYTES        (SPI_RX_FRAME_BYTES * SPI_RX_PEEK_SCAN_FRAMES)
#define SPI_RX_CONTROL_SCAN_BYTES     (SPI_RX_DMA_RING_SIZE)
#define SPI_RX_NEAR_FULL_THRESHOLD    (SPI_RX_DMA_RING_SIZE - (SPI_RX_FRAME_BYTES * 16u))
#define SPI_INPUT_CMD                 (0x06u)

static uint8_t s_spi_tx_buf[96];
static uint16_t s_spi_tx_len;
static uint16_t s_spi_tx_pos;
static uint8_t s_spi_tx_pending;
static uint32_t s_spi_tx_start_us;
static volatile uint32_t s_spi_tx_recover_count;
static volatile uint32_t s_now_us;

__attribute__((aligned(4))) static uint8_t s_spi_rx_dma_buf[SPI_RX_DMA_RING_SIZE];
static volatile uint32_t s_spi_rx_dma_wrap_count;
static uint32_t s_spi_rx_base_abs;
static uint32_t s_spi_rx_read_abs;
static uint32_t s_spi_rx_seen_wrap_count;
static uint32_t s_spi_rx_last_write_pos;
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
static volatile uint32_t s_spi_rx_peek_ok_count;
static volatile uint32_t s_spi_rx_peek_miss_count;
static volatile uint32_t s_spi_rx_direct_count;
static uint32_t s_spi_rx_control_scan_abs;

static uint32_t spi_rx_write_abs_snapshot(uint32_t *available_out);

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

static uint8_t spi_rx_dma_byte(uint32_t pos)
{
    if (pos >= SPI_RX_DMA_RING_SIZE) {
        pos -= SPI_RX_DMA_RING_SIZE;
    }
    return s_spi_rx_dma_buf[pos];
}

static uint32_t spi_rx_ring_sub(uint32_t pos, uint32_t delta)
{
    delta %= SPI_RX_DMA_RING_SIZE;
    if (pos >= delta) {
        return pos - delta;
    }
    return SPI_RX_DMA_RING_SIZE - (delta - pos);
}

static bool spi_rx_copy_frame_fast(uint32_t start, uint8_t *payload)
{
    uint32_t payload_start;
    uint32_t first_len;
    uint8_t sum = 0u;
    uint8_t i;

    if (s_spi_rx_dma_buf[start] != RFM_SPI_SYNC) {
        return false;
    }
    if (spi_rx_dma_byte(start + 1u) != SPI_INPUT_CMD) {
        return false;
    }
    if (spi_rx_dma_byte(start + 2u) != RFM_RF_INPUT_PAYLOAD_LEN) {
        return false;
    }
    for (i = 0u; i < (uint8_t)(SPI_RX_FRAME_BYTES - 1u); ++i) {
        sum = (uint8_t)(sum + spi_rx_dma_byte(start + i));
    }
    if (sum != spi_rx_dma_byte(start + SPI_RX_FRAME_BYTES - 1u)) {
        return false;
    }

    payload_start = start + 3u;
    if (payload_start >= SPI_RX_DMA_RING_SIZE) {
        payload_start -= SPI_RX_DMA_RING_SIZE;
    }
    if ((payload_start + RFM_RF_INPUT_PAYLOAD_LEN) <= SPI_RX_DMA_RING_SIZE) {
        memcpy(payload, &s_spi_rx_dma_buf[payload_start], RFM_RF_INPUT_PAYLOAD_LEN);
        return true;
    }

    first_len = SPI_RX_DMA_RING_SIZE - payload_start;
    memcpy(payload, &s_spi_rx_dma_buf[payload_start], first_len);
    memcpy(&payload[first_len], s_spi_rx_dma_buf, RFM_RF_INPUT_PAYLOAD_LEN - first_len);
    return true;
}

static uint8_t spi_rx_host_cmd_valid(uint8_t cmd)
{
    switch(cmd)
    {
    case 0x01u:
    case 0x02u:
    case 0x03u:
    case 0x04u:
    case 0x05u:
    case 0x06u:
        return 1u;
    default:
        return 0u;
    }
}

static uint32_t spi_rx_abs_pos(uint32_t abs_pos)
{
    return (abs_pos - s_spi_rx_base_abs) % SPI_RX_DMA_RING_SIZE;
}

static uint8_t spi_rx_abs_byte(uint32_t abs_pos)
{
    return spi_rx_dma_byte(spi_rx_abs_pos(abs_pos));
}

static uint8_t spi_rx_frame_checksum_valid(uint32_t start_abs, uint8_t total)
{
    uint8_t sum = 0u;
    uint8_t i;

    for(i = 0u; i < (uint8_t)(total - 1u); ++i)
    {
        sum = (uint8_t)(sum + spi_rx_abs_byte(start_abs + i));
    }

    return (sum == spi_rx_abs_byte(start_abs + total - 1u)) ? 1u : 0u;
}

static void spi_rx_copy_abs_frame(uint32_t start_abs, uint8_t total, uint8_t *frame)
{
    uint8_t i;

    for(i = 0u; i < total; ++i)
    {
        frame[i] = spi_rx_abs_byte(start_abs + i);
    }
}

bool rfm_spi_port_peek_latest_control_frame(uint8_t *frame, uint8_t *inout_len)
{
    uint32_t available;
    uint32_t write_abs;
    uint32_t oldest_abs;
    uint32_t latest_scan_abs;
    uint32_t scan_end_abs;

    if((frame == 0) || (inout_len == 0) || (*inout_len < 4u))
    {
        return false;
    }
    if(s_spi_tx_pending != 0u)
    {
        return false;
    }

    write_abs = spi_rx_write_abs_snapshot(&available);
    if((write_abs - s_spi_rx_base_abs) > SPI_RX_DMA_RING_SIZE)
    {
        oldest_abs = write_abs - SPI_RX_DMA_RING_SIZE;
    }
    else
    {
        oldest_abs = s_spi_rx_base_abs;
    }

    if(s_spi_rx_control_scan_abs < oldest_abs)
    {
        s_spi_rx_control_scan_abs = oldest_abs;
    }
    if(s_spi_rx_control_scan_abs > write_abs)
    {
        s_spi_rx_control_scan_abs = write_abs;
    }

    latest_scan_abs = oldest_abs;
    if((write_abs - latest_scan_abs) > SPI_RX_CONTROL_SCAN_BYTES)
    {
        latest_scan_abs = write_abs - SPI_RX_CONTROL_SCAN_BYTES;
    }
    if(s_spi_rx_control_scan_abs < latest_scan_abs)
    {
        s_spi_rx_control_scan_abs = latest_scan_abs;
    }

    scan_end_abs = write_abs;
    if((scan_end_abs - s_spi_rx_control_scan_abs) > SPI_RX_CONTROL_SCAN_BYTES)
    {
        scan_end_abs = s_spi_rx_control_scan_abs + SPI_RX_CONTROL_SCAN_BYTES;
    }

    while(s_spi_rx_control_scan_abs < scan_end_abs)
    {
        uint8_t cmd;
        uint8_t payload_len;
        uint8_t total;
        uint32_t remaining;

        if(spi_rx_abs_byte(s_spi_rx_control_scan_abs) != RFM_SPI_SYNC)
        {
            s_spi_rx_control_scan_abs++;
            continue;
        }

        remaining = write_abs - s_spi_rx_control_scan_abs;
        if(remaining < 3u)
        {
            break;
        }

        cmd = spi_rx_abs_byte(s_spi_rx_control_scan_abs + 1u);
        if(spi_rx_host_cmd_valid(cmd) == 0u)
        {
            s_spi_rx_control_scan_abs++;
            continue;
        }

        payload_len = spi_rx_abs_byte(s_spi_rx_control_scan_abs + 2u);
        total = (uint8_t)(3u + payload_len + 1u);
        if((total < 4u) || (total > RFM_SPI_MAX_FRAME))
        {
            s_spi_rx_control_scan_abs++;
            continue;
        }
        if(remaining < (uint32_t)total)
        {
            break;
        }
        if(spi_rx_frame_checksum_valid(s_spi_rx_control_scan_abs, total) == 0u)
        {
            s_spi_rx_control_scan_abs++;
            continue;
        }

        if(cmd == SPI_INPUT_CMD)
        {
            s_spi_rx_control_scan_abs += total;
            continue;
        }

        if(total > *inout_len)
        {
            return false;
        }

        spi_rx_copy_abs_frame(s_spi_rx_control_scan_abs, total, frame);
        s_spi_rx_control_scan_abs += total;
        *inout_len = total;
        return true;
    }

    (void)available;
    return false;
}

static uint32_t spi_rx_write_abs_snapshot(uint32_t *available_out)
{
    uint32_t wraps;
    uint32_t write_pos;
    uint32_t write_local_abs;
    uint32_t write_abs;
    uint32_t available;
    uint8_t flags;

    PFIC_DisableIRQ(SPI0_IRQn);
    flags = R8_SPI0_INT_FLAG;
    write_pos = spi_rx_dma_pos();

    if ((flags & (RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END)) != 0u) {
        R8_SPI0_INT_FLAG = (uint8_t)(flags & (RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END));
        s_spi_rx_dma_wrap_count++;
        s_spi_rx_last_flags = flags;
    } else if (write_pos < s_spi_rx_last_write_pos) {
        s_spi_rx_dma_wrap_count++;
    }
    s_spi_rx_last_write_pos = write_pos;
    wraps = s_spi_rx_dma_wrap_count;
    PFIC_EnableIRQ(SPI0_IRQn);

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

    if (available_out != 0) {
        *available_out = available;
    }
    s_spi_rx_total_bytes = write_abs;
    return write_abs;
}

bool rfm_spi_port_peek_latest_input(uint8_t *payload, uint8_t len)
{
    uint32_t write_pos;
    uint32_t scan_len;
    uint32_t offset;

    if ((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN)) {
        s_spi_rx_peek_miss_count++;
        return false;
    }

    write_pos = spi_rx_dma_pos();
    scan_len = SPI_RX_PEEK_SCAN_BYTES;
    if (scan_len > SPI_RX_DMA_RING_SIZE) {
        scan_len = SPI_RX_DMA_RING_SIZE;
    }

    for (offset = SPI_RX_FRAME_BYTES; offset <= scan_len; ++offset) {
        uint32_t start = spi_rx_ring_sub(write_pos, offset);

        if (spi_rx_copy_frame_fast(start, payload)) {
            s_spi_rx_peek_ok_count++;
            s_spi_rx_direct_count++;
            return true;
        }
    }

    s_spi_rx_peek_miss_count++;
    return false;
}

static void spi_rx_restart_after_tx(void)
{
    PFIC_DisableIRQ(SPI0_IRQn);
    s_spi_rx_base_abs = s_spi_rx_total_bytes;
    s_spi_rx_dma_wrap_count = 0u;
    s_spi_rx_seen_wrap_count = 0u;
    s_spi_rx_read_abs = s_spi_rx_base_abs;
    s_spi_rx_control_scan_abs = s_spi_rx_base_abs;
    s_spi_rx_last_write_pos = 0u;
    memset(s_spi_rx_dma_buf, 0xFF, sizeof(s_spi_rx_dma_buf));
    spi_rx_dma_loop_start(1u);
    SPI0_ITCfg(ENABLE, SPI0_IT_FIFO_OV);
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
    s_spi_tx_recover_count = 0u;
    s_spi_tx_len = 0u;
    s_spi_tx_pos = 0u;
    s_now_us = 0u;
    s_spi_rx_dma_wrap_count = 0u;
    s_spi_rx_base_abs = 0u;
    s_spi_rx_seen_wrap_count = 0u;
    s_spi_rx_read_abs = 0u;
    s_spi_rx_last_write_pos = 0u;
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
    s_spi_rx_peek_ok_count = 0u;
    s_spi_rx_peek_miss_count = 0u;
    s_spi_rx_direct_count = 0u;
    s_spi_rx_control_scan_abs = 0u;
    memset(s_spi_rx_dma_buf, 0xFF, sizeof(s_spi_rx_dma_buf));

    spi_rx_dma_loop_start(1u);
    SPI0_ITCfg(ENABLE, SPI0_IT_FIFO_OV);
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

void rfm_spi_port_service(void)
{
    uint32_t available;

    if (s_spi_tx_pending != 0u) {
        spi_tx_fill_fifo();
        if ((R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END) != 0u) {
            R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
            s_spi_tx_pending = 0u;
            rfm_spi_port_set_irq(false);
            spi_rx_restart_after_tx();
        } else if (GPIOB_ReadPortPin(GPIO_Pin_12) &&
                   ((int32_t)(spi_now_us() - (s_spi_tx_start_us + SPI_TX_PENDING_RECOVER_US)) >= 0)) {
            s_spi_tx_pending = 0u;
            s_spi_tx_recover_count++;
            rfm_spi_port_set_irq(false);
            spi_rx_restart_after_tx();
        }
        return;
    }

    (void)spi_rx_write_abs_snapshot(&available);
}

size_t rfm_spi_port_drain(uint8_t *buf, size_t max_len)
{
    size_t n = 0u;
    uint32_t write_abs;
    uint32_t available;
    uint32_t read_pos;
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
                s_spi_tx_recover_count++;
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

    write_abs = spi_rx_write_abs_snapshot(&available);

    if (available > max_len) {
        uint32_t drop = available - (uint32_t)max_len;

        s_spi_rx_read_abs += drop;
        s_spi_rx_backlog_drop_count++;
        s_spi_rx_backlog_drop_bytes += drop;
        available = (uint32_t)max_len;
    }

    if (available > SPI_RX_NEAR_FULL_THRESHOLD) {
        s_spi_rx_near_full_count++;
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
    return s_spi_rx_direct_count;
}

uint32_t rfm_spi_port_rx_peek_ok_count(void)
{
    return s_spi_rx_peek_ok_count;
}

uint32_t rfm_spi_port_rx_peek_miss_count(void)
{
    return s_spi_rx_peek_miss_count;
}

uint8_t rfm_spi_port_tx_pending(void)
{
    return s_spi_tx_pending;
}

uint32_t rfm_spi_port_tx_recover_count(void)
{
    return s_spi_tx_recover_count;
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
    const uint8_t active = (uint8_t)(flags & RB_SPI_IF_FIFO_OV);
    const uint8_t noise = (uint8_t)(flags & (RB_SPI_IF_CNT_END |
                                             RB_SPI_IF_DMA_END |
                                             RB_SPI_IF_BYTE_END |
                                             RB_SPI_IF_FIFO_HF |
                                             RB_SPI_IF_FST_BYTE));

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
    if (s_spi_tx_pending != 0u) {
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
