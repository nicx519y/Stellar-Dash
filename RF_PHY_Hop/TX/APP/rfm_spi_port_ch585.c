#include "rfm_spi_port_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "CH58x_common.h"
#include "rfm_config.h"
#include "wchrf.h"

#define SPI_PINS                      (GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15)
#define SPI_IRQ_PIN                   (GPIO_Pin_11)
#define SPI_WAKE_PIN                  (GPIO_Pin_11)
#define SPI_WAKE_USE_INTX             0u
#define SPI_TX_PENDING_RECOVER_US     (2000u)
#define US_TICK_STEP                  (10u)
#define SPI_INPUT_CMD                 (0x06u)
#define SPI_RX_FRAME_BYTES            (3u + RFM_RF_INPUT_PAYLOAD_LEN + 1u)
#define SPI_RX_DMA_BUF_SIZE           1024u
#define SPI_CONTROL_SLOT_COUNT        2u

static uint8_t s_spi_tx_buf[96];
static uint16_t s_spi_tx_len;
static uint16_t s_spi_tx_pos;
static uint8_t s_spi_tx_pending;
static uint32_t s_spi_tx_start_us;
static volatile uint32_t s_spi_tx_recover_count;
static volatile uint32_t s_spi_tx_done_count;
static volatile uint32_t s_now_us;
static uint8_t s_sleep_idle_valid;
static uint32_t s_sleep_idle_pos;
static uint32_t s_sleep_idle_since_us;
static volatile uint32_t s_wake_irq_count;
static uint8_t s_sleep_block_flags;
static uint8_t s_sleep_rx_quiesced;

#if (RFM_TX_LOG_ENABLE == 1u)
static void spi_port_log_write(const char *buf)
{
    if(buf == 0)
    {
        return;
    }
    while(*buf != '\0')
    {
        while(R8_UART0_TFC == UART_FIFO_SIZE)
        {
        }
        R8_UART0_THR = (uint8_t)*buf++;
    }
}

static void spi_port_log_printf(const char *fmt, ...)
{
    char line[128];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if(n <= 0)
    {
        return;
    }
    line[sizeof(line) - 1u] = '\0';
    spi_port_log_write(line);
}

static void spi_port_log_flush(void)
{
    while((R8_UART0_LSR & RB_LSR_TX_ALL_EMP) == 0u)
    {
    }
}

#define SPI_PORT_LOG(fmt, ...) spi_port_log_printf("[SPI][PORT] " fmt "\r\n", ##__VA_ARGS__)
#define SPI_PORT_LOG_FLUSH() spi_port_log_flush()
#else
#define SPI_PORT_LOG(fmt, ...) ((void)0)
#define SPI_PORT_LOG_FLUSH() ((void)0)
#endif

__attribute__((aligned(4))) static uint8_t s_spi_rx_dma_buf[SPI_RX_DMA_BUF_SIZE];
static uint32_t s_spi_rx_dma_last_pos;
static volatile uint8_t s_spi_rx_latest_payload[RFM_RF_INPUT_PAYLOAD_LEN];
static volatile uint8_t s_spi_rx_latest_valid;
static volatile uint32_t s_spi_rx_latest_gen;

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
static volatile uint32_t s_spi_rx_done_count;
static volatile uint32_t s_spi_rx_valid_frame_count;
static volatile uint32_t s_spi_rx_bad_frame_count;
static volatile uint8_t s_spi_rx_wrap_pending;
static volatile uint8_t s_spi_rx_input_seq_valid;
static volatile uint8_t s_spi_rx_last_input_seq;

typedef enum
{
    SPI_CONTROL_WAIT_SYNC = 0u,
    SPI_CONTROL_CMD,
    SPI_CONTROL_LEN,
    SPI_CONTROL_PAYLOAD,
    SPI_CONTROL_CHECKSUM
} spi_control_parse_state_t;

static spi_control_parse_state_t s_spi_control_state;
static uint8_t s_spi_control_buf[RFM_SPI_MAX_FRAME];
static uint8_t s_spi_control_idx;
static uint8_t s_spi_control_payload_len;
static uint8_t s_spi_control_sum;
static uint8_t s_spi_control_slot[SPI_CONTROL_SLOT_COUNT][RFM_SPI_MAX_FRAME];
static uint8_t s_spi_control_slot_len[SPI_CONTROL_SLOT_COUNT];
static uint8_t s_spi_control_head;
static uint8_t s_spi_control_tail;
static uint8_t s_spi_control_count;

static void spi_rx_dma_loop_start(uint8_t flush_fifo);

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
    case 0x07u:
    case 0x08u:
        return 1u;
    default:
        return 0u;
    }
}

static uint32_t spi_now_us(void)
{
    s_now_us += US_TICK_STEP;
    return s_now_us;
}

static uint32_t spi_rx_dma_pos(void)
{
    uint32_t now = R32_SPI0_DMA_NOW;
    const uint32_t beg = (uint32_t)s_spi_rx_dma_buf;
    const uint32_t end = (uint32_t)(s_spi_rx_dma_buf + SPI_RX_DMA_BUF_SIZE);

    if((now < beg) || (now > end))
    {
        return 0u;
    }
    now -= beg;
    if(now >= SPI_RX_DMA_BUF_SIZE)
    {
        now = 0u;
    }
    return now;
}

static uint8_t spi_rx_latest_payload_same(const uint8_t *payload)
{
    uint8_t i;

    if(s_spi_rx_latest_valid == 0u)
    {
        return 0u;
    }
    for(i = 0u; i < RFM_RF_INPUT_PAYLOAD_LEN; ++i)
    {
        if(s_spi_rx_latest_payload[i] != payload[i])
        {
            return 0u;
        }
    }
    return 1u;
}

static void spi_rx_commit_latest_payload(const uint8_t *payload)
{
    uint8_t i;

    if(spi_rx_latest_payload_same(payload) != 0u)
    {
        return;
    }

    s_spi_rx_latest_gen++;
    for(i = 0u; i < RFM_RF_INPUT_PAYLOAD_LEN; ++i)
    {
        s_spi_rx_latest_payload[i] = payload[i];
    }
    s_spi_rx_latest_valid = 1u;
    s_spi_rx_latest_gen++;
    s_spi_rx_direct_count++;
}

static uint8_t spi_rx_input_seq_fresh(const uint8_t *payload)
{
    const uint8_t seq = payload[0];
    uint8_t diff;

    if(s_spi_rx_input_seq_valid == 0u)
    {
        s_spi_rx_input_seq_valid = 1u;
        s_spi_rx_last_input_seq = seq;
        return 1u;
    }

    diff = (uint8_t)(seq - s_spi_rx_last_input_seq);
    if((diff == 0u) || (diff >= 128u))
    {
        return 0u;
    }

    s_spi_rx_last_input_seq = seq;
    return 1u;
}

static void spi_control_parser_reset(void)
{
    s_spi_control_state = SPI_CONTROL_WAIT_SYNC;
    s_spi_control_idx = 0u;
    s_spi_control_payload_len = 0u;
    s_spi_control_sum = 0u;
}

static void spi_control_parser_start(void)
{
    s_spi_control_buf[0] = RFM_SPI_SYNC;
    s_spi_control_idx = 1u;
    s_spi_control_payload_len = 0u;
    s_spi_control_sum = RFM_SPI_SYNC;
    s_spi_control_state = SPI_CONTROL_CMD;
}

static void spi_control_slot_push(const uint8_t *frame, uint8_t len)
{
    if((frame == 0) || (len == 0u) || (len > RFM_SPI_MAX_FRAME))
    {
        return;
    }

    memcpy(s_spi_control_slot[s_spi_control_head], frame, len);
    s_spi_control_slot_len[s_spi_control_head] = len;
    s_spi_control_head++;
    if(s_spi_control_head >= SPI_CONTROL_SLOT_COUNT)
    {
        s_spi_control_head = 0u;
    }
    if(s_spi_control_count < SPI_CONTROL_SLOT_COUNT)
    {
        s_spi_control_count++;
    }
    else
    {
        s_spi_control_tail = s_spi_control_head;
        s_spi_rx_backlog_drop_count++;
        s_spi_rx_backlog_drop_bytes += len;
    }
}

static void spi_control_parser_feed(uint8_t b)
{
    switch(s_spi_control_state)
    {
    case SPI_CONTROL_WAIT_SYNC:
        if(b == RFM_SPI_SYNC)
        {
            spi_control_parser_start();
        }
        break;

    case SPI_CONTROL_CMD:
        if(spi_rx_host_cmd_valid(b) == 0u)
        {
            if(b == RFM_SPI_SYNC)
            {
                spi_control_parser_start();
            }
            else
            {
                spi_control_parser_reset();
            }
            break;
        }
        s_spi_control_buf[s_spi_control_idx++] = b;
        s_spi_control_sum = (uint8_t)(s_spi_control_sum + b);
        s_spi_control_state = SPI_CONTROL_LEN;
        break;

    case SPI_CONTROL_LEN:
        if(((uint16_t)3u + (uint16_t)b + (uint16_t)1u) > RFM_SPI_MAX_FRAME)
        {
            s_spi_rx_bad_frame_count++;
            spi_control_parser_reset();
            break;
        }
        s_spi_control_payload_len = b;
        s_spi_control_buf[s_spi_control_idx++] = b;
        s_spi_control_sum = (uint8_t)(s_spi_control_sum + b);
        s_spi_control_state = (b == 0u) ? SPI_CONTROL_CHECKSUM : SPI_CONTROL_PAYLOAD;
        break;

    case SPI_CONTROL_PAYLOAD:
        s_spi_control_buf[s_spi_control_idx++] = b;
        s_spi_control_sum = (uint8_t)(s_spi_control_sum + b);
        if(s_spi_control_idx >= (uint8_t)(3u + s_spi_control_payload_len))
        {
            s_spi_control_state = SPI_CONTROL_CHECKSUM;
        }
        break;

    case SPI_CONTROL_CHECKSUM:
        s_spi_control_buf[s_spi_control_idx++] = b;
        if(s_spi_control_sum == b)
        {
            if((s_spi_control_buf[1] == SPI_INPUT_CMD) &&
               (s_spi_control_payload_len == RFM_RF_INPUT_PAYLOAD_LEN))
            {
                if(spi_rx_input_seq_fresh(&s_spi_control_buf[3]) != 0u)
                {
                    s_spi_rx_done_count++;
                    s_spi_rx_valid_frame_count++;
                    spi_rx_commit_latest_payload(&s_spi_control_buf[3]);
                }
            }
            else if(s_spi_control_buf[1] != SPI_INPUT_CMD)
            {
                s_spi_rx_done_count++;
                spi_control_slot_push(s_spi_control_buf, s_spi_control_idx);
            }
            else
            {
                s_spi_rx_bad_frame_count++;
            }
        }
        else
        {
            s_spi_rx_bad_frame_count++;
        }
        spi_control_parser_reset();
        break;

    default:
        spi_control_parser_reset();
        break;
    }
}

static void spi_rx_note_advance(uint32_t from, uint32_t delta)
{
    uint32_t pos = from;
    uint32_t i;

    for(i = 0u; i < delta; ++i)
    {
        spi_control_parser_feed(s_spi_rx_dma_buf[pos]);
        pos++;
        if(pos >= SPI_RX_DMA_BUF_SIZE)
        {
            pos = 0u;
        }
    }
}

static void spi_rx_dma_poll(void)
{
    const uint32_t pos = spi_rx_dma_pos();
    uint8_t flags;
    uint8_t loop_end;
    uint32_t delta;

    flags = R8_SPI0_INT_FLAG;
    loop_end = (uint8_t)(flags & (RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END));
    if(loop_end != 0u)
    {
        R8_SPI0_INT_FLAG = loop_end;
    }
    s_spi_rx_last_flags = flags;

    if((flags & RB_SPI_IF_FIFO_OV) != 0u)
    {
        R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
        s_spi_rx_fifo_ov_count++;
        s_spi_rx_ring_overrun_count++;
        spi_rx_dma_loop_start(1u);
        return;
    }

    if((pos == s_spi_rx_dma_last_pos) && (loop_end != 0u))
    {
        delta = SPI_RX_DMA_BUF_SIZE;
    }
    else if(pos >= s_spi_rx_dma_last_pos)
    {
        delta = pos - s_spi_rx_dma_last_pos;
    }
    else
    {
        delta = (SPI_RX_DMA_BUF_SIZE - s_spi_rx_dma_last_pos) + pos;
    }

    if(delta != 0u)
    {
        spi_rx_note_advance(s_spi_rx_dma_last_pos, delta);
        s_spi_rx_total_bytes += delta;
        if(delta > s_spi_rx_max_available)
        {
            s_spi_rx_max_available = delta;
        }
        s_spi_rx_dma_last_pos = pos;
    }
}

static void spi_rx_dma_state_reset(void)
{
    s_spi_rx_dma_last_pos = 0u;
    s_spi_rx_wrap_pending = 0u;
    s_spi_control_head = 0u;
    s_spi_control_tail = 0u;
    s_spi_control_count = 0u;
    s_spi_rx_input_seq_valid = 0u;
    s_spi_rx_last_input_seq = 0u;
    memset(s_spi_control_slot_len, 0, sizeof(s_spi_control_slot_len));
    spi_control_parser_reset();
}

static void spi_rx_dma_loop_start(uint8_t flush_fifo)
{
    R8_SPI0_CTRL_CFG &= (uint8_t)(~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP));
    R8_SPI0_CTRL_MOD = (uint8_t)((R8_SPI0_CTRL_MOD | RB_SPI_FIFO_DIR) &
                                 (uint8_t)(~RB_SPI_SLV_CMD_MOD));
    if(flush_fifo != 0u)
    {
        while(R8_SPI0_FIFO_COUNT != 0u)
        {
            (void)R8_SPI0_FIFO;
        }
    }

    if(flush_fifo != 0u)
    {
        memset(s_spi_rx_dma_buf, 0xFF, sizeof(s_spi_rx_dma_buf));
        spi_rx_dma_state_reset();
        s_spi_rx_total_bytes = 0u;
    }

    R32_SPI0_DMA_BEG = (uint32_t)s_spi_rx_dma_buf;
    R32_SPI0_DMA_END = (uint32_t)(s_spi_rx_dma_buf + SPI_RX_DMA_BUF_SIZE);
    R32_SPI0_DMA_NOW = (uint32_t)s_spi_rx_dma_buf;
    R16_SPI0_TOTAL_CNT = SPI_RX_DMA_BUF_SIZE;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END | RB_SPI_IF_FIFO_OV |
                       RB_SPI_IF_FIFO_HF | RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE;
    R8_SPI0_CTRL_CFG |= (uint8_t)(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_OV |
                         SPI0_IT_FIFO_HF | SPI0_IT_BYTE_END | SPI0_IT_FST_BYTE);
    PFIC_DisableIRQ(SPI0_IRQn);
}

static void spi_rx_restart_after_tx(void)
{
    PFIC_DisableIRQ(SPI0_IRQn);
    spi_rx_dma_loop_start(1u);
}

static void spi_tx_fill_fifo(void)
{
    while((s_spi_tx_pos < s_spi_tx_len) && (R8_SPI0_FIFO_COUNT < SPI_FIFO_SIZE))
    {
        R8_SPI0_FIFO = s_spi_tx_buf[s_spi_tx_pos++];
    }
}

void rfm_spi_port_init(void)
{
    uint8_t i;

    GPIOPinRemap(ENABLE, RB_PIN_SPI0);
    GPIOADigitalCfg(ENABLE, (uint16_t)0xFFFFu);
    GPIOBDigitalCfg(ENABLE, SPI_PINS | SPI_IRQ_PIN | SPI_WAKE_PIN);
    GPIOB_ModeCfg(SPI_IRQ_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(SPI_IRQ_PIN);

    GPIOB_ModeCfg(GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeOut_PP_5mA);

    SPI0_SlaveInit();
    R8_SPI0_CTRL_MOD = (uint8_t)((R8_SPI0_CTRL_MOD | RB_SPI_FIFO_DIR) &
                                 (uint8_t)(~RB_SPI_SLV_CMD_MOD));

    s_spi_tx_pending = 0u;
    s_spi_tx_start_us = 0u;
    s_spi_tx_recover_count = 0u;
    s_spi_tx_done_count = 0u;
    s_spi_tx_len = 0u;
    s_spi_tx_pos = 0u;
    s_now_us = 0u;
    s_sleep_idle_valid = 0u;
    s_sleep_idle_pos = 0u;
    s_sleep_idle_since_us = 0u;
    s_sleep_rx_quiesced = 0u;
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
    s_spi_rx_done_count = 0u;
    s_spi_rx_valid_frame_count = 0u;
    s_spi_rx_bad_frame_count = 0u;
    s_spi_rx_wrap_pending = 0u;
    s_spi_rx_input_seq_valid = 0u;
    s_spi_rx_last_input_seq = 0u;
    s_spi_rx_latest_valid = 0u;
    s_spi_rx_latest_gen = 0u;
    for(i = 0u; i < RFM_RF_INPUT_PAYLOAD_LEN; ++i)
    {
        s_spi_rx_latest_payload[i] = 0u;
    }

    spi_rx_dma_loop_start(1u);
}

static void clear_wake_it_flag(void)
{
    const uint16_t low_pin_mask = (uint16_t)(SPI_WAKE_PIN & 0xFFFFu);
    const uint16_t remap_pin_mask = (uint16_t)((SPI_WAKE_PIN & (GPIO_Pin_22 | GPIO_Pin_23)) >> 14);
    R16_PB_INT_IF = (uint16_t)(low_pin_mask | remap_pin_mask);
}

bool rfm_spi_port_sleep_ready(uint16_t stable_us)
{
    const uint32_t now_us = spi_now_us();
    const uint32_t pos = spi_rx_dma_pos();

    if(s_sleep_rx_quiesced == 0u)
    {
        spi_rx_dma_poll();
    }
    s_sleep_block_flags = 0u;
    if(s_spi_tx_pending != 0u)
    {
        s_sleep_block_flags |= 0x01u;
    }
    if(s_spi_control_count != 0u)
    {
        s_sleep_block_flags |= 0x02u;
    }
    if(GPIOB_ReadPortPin(GPIO_Pin_12) == 0u)
    {
        s_sleep_block_flags |= 0x04u;
    }
    if((s_spi_tx_pending != 0u) ||
       (s_spi_control_count != 0u) ||
       (GPIOB_ReadPortPin(GPIO_Pin_12) == 0u))
    {
        s_sleep_idle_valid = 0u;
        return false;
    }

    if((s_sleep_idle_valid == 0u) || (s_sleep_idle_pos != pos))
    {
        s_sleep_idle_valid = 1u;
        s_sleep_idle_pos = pos;
        s_sleep_idle_since_us = now_us;
        s_sleep_block_flags |= 0x08u;
        return false;
    }

    if(((uint32_t)(now_us - s_sleep_idle_since_us) < (uint32_t)stable_us))
    {
        s_sleep_block_flags |= 0x10u;
        return false;
    }

    s_sleep_block_flags = 0u;
    return true;
}

uint8_t rfm_spi_port_sleep_block_flags(void)
{
    return s_sleep_block_flags;
}

void rfm_spi_port_sleep_until_nss_wake(void)
{
    rfm_spi_port_set_irq(false);
    PFIC_DisableIRQ(SPI0_IRQn);
    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_OV |
                         SPI0_IT_FIFO_HF | SPI0_IT_BYTE_END | SPI0_IT_FST_BYTE);
    R8_SPI0_CTRL_CFG &= (uint8_t)(~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP));
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END | RB_SPI_IF_FIFO_OV |
                       RB_SPI_IF_FIFO_HF | RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE;
    s_spi_tx_pending = 0u;
    s_spi_tx_len = 0u;
    s_spi_tx_pos = 0u;

    while(R8_SPI0_FIFO_COUNT != 0u)
    {
        (void)R8_SPI0_FIFO;
    }

    GPIOPinRemap(DISABLE, RB_PIN_SPI0);
    GPIOBDigitalCfg(ENABLE, SPI_PINS | SPI_IRQ_PIN | SPI_WAKE_PIN);
#if (SPI_WAKE_USE_INTX != 0u)
    GPIOPinRemap(ENABLE, RB_PIN_INTX);
#endif
    GPIOB_ModeCfg(GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(SPI_IRQ_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(SPI_IRQ_PIN);
    GPIOB_ModeCfg(SPI_WAKE_PIN, GPIO_ModeIN_PU);

    clear_wake_it_flag();
    SPI_PORT_LOG("SLEEP_PREP pb11=%u mode=sleep_lowlevel", GPIOB_ReadPortPin(SPI_WAKE_PIN) != 0u ? 1u : 0u);
    if(GPIOB_ReadPortPin(SPI_WAKE_PIN) == 0u)
    {
#if (SPI_WAKE_USE_INTX != 0u)
        GPIOPinRemap(DISABLE, RB_PIN_INTX);
#endif
        rfm_spi_port_init();
        return;
    }
    GPIOB_ITModeCfg(SPI_WAKE_PIN, GPIO_ITMode_LowLevel);
    PFIC_EnableIRQ(GPIO_B_IRQn);
    PWR_PeriphWakeUpCfg(ENABLE, RB_SLP_GPIO_WAKE | RB_GPIO_EDGE_WAKE, Long_Delay);

    SPI_PORT_LOG("SLEEP_ENTER_SLEEP pb11=%u irq_count=%lu",
                 GPIOB_ReadPortPin(SPI_WAKE_PIN) != 0u ? 1u : 0u,
                 (uint32_t)s_wake_irq_count);
    SPI_PORT_LOG_FLUSH();
    DelayMs(100);
    LowPower_Sleep(RB_PWR_RAM32K | RB_PWR_RAM96K | RB_PWR_EXTEND);
    SPI_PORT_LOG("WAKE_RETURN_SLEEP pb11=%u irq_count=%lu",
                 GPIOB_ReadPortPin(SPI_WAKE_PIN) != 0u ? 1u : 0u,
                 (uint32_t)s_wake_irq_count);

    SetSysClock(SYSCLK_FREQ);

    RFIP_WakeUpRegInit();
    PFIC_DisableIRQ(GPIO_B_IRQn);
    clear_wake_it_flag();
    PWR_PeriphWakeUpCfg(DISABLE, RB_SLP_GPIO_WAKE | RB_GPIO_EDGE_WAKE, Long_Delay);
#if (SPI_WAKE_USE_INTX != 0u)
    GPIOPinRemap(DISABLE, RB_PIN_INTX);
#endif
    rfm_spi_port_init();
}

__INTERRUPT
__HIGH_CODE
void GPIOB_IRQHandler(void)
{
    s_wake_irq_count++;
    clear_wake_it_flag();
    PFIC_DisableIRQ(GPIO_B_IRQn);
}

void rfm_spi_port_set_irq(bool asserted)
{
    if(asserted)
    {
        GPIOB_SetBits(SPI_IRQ_PIN);
    }
    else
    {
        GPIOB_ResetBits(SPI_IRQ_PIN);
    }
}

void rfm_spi_port_service(void)
{
    if(s_spi_tx_pending != 0u)
    {
        spi_tx_fill_fifo();
        if((R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END) != 0u)
        {
            R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
            s_spi_tx_pending = 0u;
            s_spi_tx_done_count++;
            spi_rx_restart_after_tx();
            rfm_spi_port_set_irq(false);
        }
        else if(GPIOB_ReadPortPin(GPIO_Pin_12) &&
                ((int32_t)(spi_now_us() - (s_spi_tx_start_us + SPI_TX_PENDING_RECOVER_US)) >= 0))
        {
            s_spi_tx_pending = 0u;
            s_spi_tx_recover_count++;
            spi_rx_restart_after_tx();
            rfm_spi_port_set_irq(false);
        }
        return;
    }

    spi_rx_dma_poll();
}

bool rfm_spi_port_peek_latest_input(uint8_t *payload, uint8_t len)
{
    uint32_t gen0;
    uint32_t gen1;
    uint8_t attempts;
    uint8_t i;

    if((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN))
    {
        s_spi_rx_peek_miss_count++;
        return false;
    }
    if(s_spi_tx_pending != 0u)
    {
        s_spi_rx_peek_miss_count++;
        return false;
    }

    for(attempts = 0u; attempts < 3u; ++attempts)
    {
        gen0 = s_spi_rx_latest_gen;
        if(((gen0 & 1u) != 0u) || (s_spi_rx_latest_valid == 0u))
        {
            s_spi_rx_peek_miss_count++;
            return false;
        }

        for(i = 0u; i < RFM_RF_INPUT_PAYLOAD_LEN; ++i)
        {
            payload[i] = s_spi_rx_latest_payload[i];
        }
        gen1 = s_spi_rx_latest_gen;
        if((gen0 == gen1) && ((gen1 & 1u) == 0u))
        {
            s_spi_rx_peek_ok_count++;
            return true;
        }
    }

    s_spi_rx_peek_miss_count++;
    return false;
}

bool rfm_spi_port_peek_latest_control_frame(uint8_t *frame, uint8_t *inout_len)
{
    uint8_t len;

    if((frame == 0) || (inout_len == 0) || (*inout_len < 4u))
    {
        return false;
    }
    if(s_spi_tx_pending != 0u)
    {
        return false;
    }

    spi_rx_dma_poll();
    if(s_spi_control_count == 0u)
    {
        return false;
    }
    len = s_spi_control_slot_len[s_spi_control_tail];
    if((len == 0u) || (len > *inout_len))
    {
        return false;
    }

    memcpy(frame, s_spi_control_slot[s_spi_control_tail], len);
    s_spi_control_slot_len[s_spi_control_tail] = 0u;
    s_spi_control_tail++;
    if(s_spi_control_tail >= SPI_CONTROL_SLOT_COUNT)
    {
        s_spi_control_tail = 0u;
    }
    s_spi_control_count--;
    *inout_len = len;
    return true;
}

void rfm_spi_port_discard_control_frames(void)
{
    uint8_t i;

    if(s_sleep_rx_quiesced == 0u)
    {
        R8_SPI0_CTRL_CFG &= (uint8_t)(~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP));
        R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END | RB_SPI_IF_FIFO_OV |
                           RB_SPI_IF_FIFO_HF | RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE;
        while(R8_SPI0_FIFO_COUNT != 0u)
        {
            (void)R8_SPI0_FIFO;
        }
        s_spi_rx_dma_last_pos = spi_rx_dma_pos();
        s_sleep_idle_valid = 0u;
        s_sleep_rx_quiesced = 1u;
    }
    for(i = 0u; i < SPI_CONTROL_SLOT_COUNT; ++i)
    {
        s_spi_control_slot_len[i] = 0u;
    }
    s_spi_control_head = 0u;
    s_spi_control_tail = 0u;
    s_spi_control_count = 0u;
    spi_control_parser_reset();
}

size_t rfm_spi_port_drain(uint8_t *buf, size_t max_len)
{
    (void)buf;
    (void)max_len;
    return 0u;
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
    const uint32_t max_value = s_spi_rx_max_available;
    s_spi_rx_max_available = 0u;
    return max_value;
}

uint32_t rfm_spi_port_rx_take_near_full_count(void)
{
    const uint32_t count = s_spi_rx_near_full_count;
    s_spi_rx_near_full_count = 0u;
    return count;
}

uint32_t rfm_spi_port_rx_take_full_clip_count(void)
{
    const uint32_t count = s_spi_rx_full_clip_count;
    s_spi_rx_full_clip_count = 0u;
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

uint32_t rfm_spi_port_rx_done_count(void)
{
    return s_spi_rx_done_count;
}

uint32_t rfm_spi_port_rx_valid_frame_count(void)
{
    return s_spi_rx_valid_frame_count;
}

uint32_t rfm_spi_port_rx_bad_frame_count(void)
{
    return s_spi_rx_bad_frame_count;
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

uint32_t rfm_spi_port_tx_done_count(void)
{
    return s_spi_tx_done_count;
}

bool rfm_spi_port_try_read(uint8_t *buf, size_t *inout_len)
{
    (void)buf;
    if(inout_len != 0)
    {
        *inout_len = 0u;
    }
    return false;
}

__attribute__((interrupt("WCH-Interrupt-fast"), section(".highcode")))
void SPI0_IRQHandler(void)
{
    const uint8_t flags = R8_SPI0_INT_FLAG;

    s_spi_rx_isr_count++;
    s_spi_rx_last_flags = flags;

    if(flags == 0u)
    {
        return;
    }
    if((flags & RB_SPI_IF_FIFO_OV) != 0u)
    {
        R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
        s_spi_rx_fifo_ov_count++;
        s_spi_rx_ring_overrun_count++;
        return;
    }
    if((flags & (RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END)) != 0u)
    {
        R8_SPI0_INT_FLAG = (uint8_t)(flags & (RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END));
        if(s_spi_rx_wrap_pending != 0u)
        {
            s_spi_rx_ring_overrun_count++;
        }
        s_spi_rx_wrap_pending = 1u;
        return;
    }

    R8_SPI0_INT_FLAG = flags;
    s_spi_rx_bad_irq_count++;
}

bool rfm_spi_port_try_write(const uint8_t *buf, size_t len)
{
    size_t i;

    if((buf == 0) || (len == 0u) || (len > 4095u) || (len > sizeof(s_spi_tx_buf)))
    {
        return false;
    }
    if(s_spi_tx_pending != 0u)
    {
        return false;
    }

    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_OV);
    PFIC_DisableIRQ(SPI0_IRQn);
    R8_SPI0_CTRL_CFG &= (uint8_t)(~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP));
    R8_SPI0_CTRL_MOD &= (uint8_t)(~RB_SPI_FIFO_DIR);
    R16_SPI0_TOTAL_CNT = (uint16_t)len;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;

    while(R8_SPI0_FIFO_COUNT != 0u)
    {
        (void)R8_SPI0_FIFO;
    }

    for(i = 0u; i < len; ++i)
    {
        s_spi_tx_buf[i] = buf[i];
    }
    s_spi_tx_len = (uint16_t)len;
    s_spi_tx_pos = 0u;
    spi_tx_fill_fifo();
    s_spi_tx_pending = 1u;
    s_spi_tx_start_us = spi_now_us();
    if((len >= 2u) && (buf[0] == RFM_SPI_SYNC) && (buf[1] == 0x82u))
    {
        SPI_PORT_LOG("TX_FRAME evt=0x%02X len=%u head=%02X %02X %02X %02X %02X %02X %02X %02X",
                     (unsigned int)buf[1],
                     (unsigned int)len,
                     (unsigned int)buf[0],
                     (unsigned int)buf[1],
                     (unsigned int)((len > 2u) ? buf[2] : 0u),
                     (unsigned int)((len > 3u) ? buf[3] : 0u),
                     (unsigned int)((len > 4u) ? buf[4] : 0u),
                     (unsigned int)((len > 5u) ? buf[5] : 0u),
                     (unsigned int)((len > 6u) ? buf[6] : 0u),
                     (unsigned int)((len > 7u) ? buf[7] : 0u));
    }
    return true;
}
