#include "rfm_spi_port_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "CH58x_common.h"
#include "board_latest_ch585.h"
#include "rfm_config.h"
#include "usb_board_link_port_ch585.h"
#include "wchrf.h"
#include "rf_link_clock.h"
#include "RF_PHY.h"

#define SPI_WAKE_PIN                  RFM_BOARD_SPI_MISO_PIN
#define SPI_WAKE_USE_INTX             0u
#define SPI_TX_PENDING_RECOVER_US     (2000u)
#define SPI_INPUT_CMD                 (0x06u)
#define SPI_RX_FRAME_BYTES            (3u + RFM_RF_INPUT_PAYLOAD_LEN + 1u)
#define SPI_RX_DMA_BUF_SIZE           1024u
#define SPI_CONTROL_SLOT_COUNT        2u

static uint8_t s_spi_tx_buf[96] __attribute__((aligned(4)));
static uint16_t s_spi_tx_len;
static uint16_t s_spi_tx_pos;
static volatile uint8_t s_spi_tx_pending;
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
static uint8_t s_board_boot_ready_sent;
static volatile uint8_t s_measure_nss;
static volatile uint8_t s_measure_diag;
static uint32_t s_nss_rx_total;

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

__attribute__((aligned(4))) static volatile uint8_t s_spi_rx_dma_buf[SPI_RX_DMA_BUF_SIZE];
/* Absolute byte cursors distinguish an empty ring from a producer lap. */
static volatile uint32_t s_spi_rx_dma_wrap_bytes;
static uint32_t s_spi_rx_dma_consumed;
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
static volatile uint8_t s_spi_rx_input_end_valid;
static volatile uint32_t s_spi_rx_input_end;
/* RAM lookup: CRC remains mandatory, but NSS uses nine lookups instead of
 * eighteen nibble lookups from Flash at the input report cadence. */
static uint8_t s_spi_input_crc_table[256];

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
    case 0x09u: /* sampling trace */
    case 0x0Au: /* clock echo */
        return 1u;
    default:
        return 0u;
    }
}

static uint32_t spi_now_us(void)
{
    /* Real monotonic hardware time, independent of service-call frequency.
     * Only this port's main-loop code observes this private accumulator. */
    static rfh_cycle_clock_t clock;
    if(clock.cycles_per_tick == 0u) {
        clock.cycles_per_tick = GetSysClock() / 1000000u;
        if(clock.cycles_per_tick == 0u) clock.cycles_per_tick = 1u;
    }
    s_now_us = rfh_cycle_clock_advance(&clock, SysTick->CNT);
    return s_now_us;
}

__HIGH_CODE
static uint32_t spi_rx_dma_pos(void)
{
    /* DMA registers describe an SRAM bus offset. Normalize both operands;
     * comparing a register offset with a CPU pointer rejects valid progress. */
    uint32_t now = R32_SPI0_DMA_NOW & 0x1FFFFu;
    const uint32_t beg = (uint32_t)s_spi_rx_dma_buf & 0x1FFFFu;
    const uint32_t end = beg + SPI_RX_DMA_BUF_SIZE;

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

__HIGH_CODE
static uint32_t spi_rx_dma_produced(void)
{
    uint32_t lock, pos, total;
    /* Restore the v16 atomic snapshot. The epoch, pending wrap and DMA NOW
     * must be observed in one protected interval, including when called from
     * NSS. Do not change the interrupt nesting contract to save this lock. */
    SYS_DisableAllIrq(&lock);
    /* Account a wrap even if its IRQ is pending. Re-read NOW when DMA wraps
     * during the snapshot; never combine a pre-wrap epoch with a post-wrap
     * cursor. CNT_END is the transfer counter, not the DMA ring epoch. */
    do {
        if(R8_SPI0_INT_FLAG & RB_SPI_IF_DMA_END) {
            R8_SPI0_INT_FLAG = RB_SPI_IF_DMA_END;
            s_spi_rx_dma_wrap_bytes += SPI_RX_DMA_BUF_SIZE;
        }
        pos = spi_rx_dma_pos();
    } while(R8_SPI0_INT_FLAG & RB_SPI_IF_DMA_END);
    total = s_spi_rx_dma_wrap_bytes + pos;
    SYS_RecoverIrq(lock);
    return total;
}

__HIGH_CODE
static uint8_t spi_input_crc_valid(const uint8_t *payload)
{
    uint8_t crc = 0u;
    for(unsigned i=0; i<RFM_RF_INPUT_PAYLOAD_LEN-1u; ++i)
        crc = s_spi_input_crc_table[crc ^ payload[i]];
    return crc == payload[RFM_RF_INPUT_PAYLOAD_LEN-1u];
}

__HIGH_CODE
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

__HIGH_CODE
static void spi_rx_commit_latest_payload(const uint8_t *payload)
{
    uint8_t i;

    if(spi_rx_latest_payload_same(payload) != 0u)
    {
        return;
    }

    /* Input publication has the same bounded path with capture on or off.
     * Do not run the RF/battery/clock update inside the SPI parser. Capture
     * only the first frame identity of an edge before latest-wins coalescing. */
    /* spi_rx_accept_input holds the publication lock. */
    s_spi_rx_latest_gen++;
    for(i = 0u; i < RFM_RF_INPUT_PAYLOAD_LEN; ++i)
    {
        s_spi_rx_latest_payload[i] = payload[i];
    }
    s_spi_rx_latest_valid = 1u;
    s_spi_rx_latest_gen++;
    RF_SPI_RecordInputEdge(payload);
    s_spi_rx_direct_count++;
}

__HIGH_CODE
static void spi_rx_accept_input(const uint8_t *payload, uint32_t input_end)
{
    uint32_t lock;
    SYS_DisableAllIrq(&lock);
    /* NSS can publish a newer input while the main parser consumes older
     * copied bytes. Compare absolute stream positions, never an 8-bit sample
     * sequence which aliases after 256 samples. Keep metadata parsing ordered. */
    if(!s_spi_rx_input_end_valid || (int32_t)(input_end-s_spi_rx_input_end)>0) {
        s_spi_rx_input_end_valid=1u;
        s_spi_rx_input_end=input_end;
        ++s_spi_rx_done_count;
        ++s_spi_rx_valid_frame_count;
        spi_rx_commit_latest_payload(payload);
    }
    SYS_RecoverIrq(lock);
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

static void spi_control_parser_feed(uint8_t b, uint32_t byte_end)
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
                /* A frame already accepted by NSS was CRC checked there.
                 * It can never be published again, so do not recalculate it.
                 * New/fallback frames still undergo the full CRC check. */
                if(s_spi_rx_input_end_valid && (int32_t)(byte_end-s_spi_rx_input_end)<=0)
                {
                    /* Continue parsing any source sidecar after this frame. */
                }
                else if(!spi_input_crc_valid(&s_spi_control_buf[3]))
                {
                    s_spi_rx_bad_frame_count++;
                }
                else
                {
                    spi_rx_accept_input(&s_spi_control_buf[3],byte_end);
                }
            }
            else if(s_spi_control_buf[1] == 0x09u)
            {
                /* Source frames are immutable input metadata, not commands.
                 * Consume them in stream order before later input or a reply
                 * can overwrite/reset the two-slot command queue. */
                s_spi_rx_done_count++;
                (void)RF_SPI_WriteTrace(0x09u,&s_spi_control_buf[3],s_spi_control_payload_len);
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

static void spi_rx_dma_poll(void)
{
    uint8_t snapshot[64];
    uint32_t budget = 256u;
    uint8_t flags = R8_SPI0_INT_FLAG;
    s_spi_rx_last_flags = flags;

    if((flags & RB_SPI_IF_FIFO_OV) != 0u)
    {
        R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
        s_spi_rx_fifo_ov_count++;
        s_spi_rx_ring_overrun_count++;
        spi_rx_dma_loop_start(1u);
        return;
    }

    while(budget) {
        const uint32_t start = s_spi_rx_dma_consumed;
        uint32_t produced = spi_rx_dma_produced();
        uint32_t available = produced - start;
        if(available > s_spi_rx_max_available)s_spi_rx_max_available = available;
        if(!available)return;
        if(available < SPI_RX_DMA_BUF_SIZE) {
            uint32_t count = available < sizeof(snapshot) ? available : sizeof(snapshot);
            if(count > budget)count = budget;
            for(uint32_t i=0; i<count; ++i)
                snapshot[i] = s_spi_rx_dma_buf[(start+i) % SPI_RX_DMA_BUF_SIZE];
            /* DMA continues while copying. Validate BEFORE feeding any byte
             * to a parser that can publish input or source records. */
            produced = spi_rx_dma_produced();
            if((uint32_t)(produced-start) < SPI_RX_DMA_BUF_SIZE &&
               !(R8_SPI0_INT_FLAG & RB_SPI_IF_FIFO_OV)) {
                s_spi_rx_dma_consumed = start + count;
                s_spi_rx_total_bytes += count;
                for(uint32_t i=0; i<count; ++i)spi_control_parser_feed(snapshot[i],start+i+1u);
                budget -= count;
                continue;
            }
        }
        /* The producer overtook us: no stale complete frame may be replayed
         * just because its checksum happens to remain valid in the ring. */
        ++s_spi_rx_ring_overrun_count;
        ++s_spi_rx_backlog_drop_count;
        s_spi_rx_backlog_drop_bytes += produced - start;
        s_spi_rx_dma_consumed = produced;
        spi_control_parser_reset();
        return;
    }
}

static void spi_rx_dma_state_reset(void)
{
    s_spi_rx_dma_consumed = 0u;
    s_spi_rx_dma_wrap_bytes = 0u;
    s_spi_control_head = 0u;
    s_spi_control_tail = 0u;
    s_spi_control_count = 0u;
    s_spi_rx_input_end_valid = 0u;
    s_spi_rx_input_end = 0u;
    memset(s_spi_control_slot_len, 0, sizeof(s_spi_control_slot_len));
    spi_control_parser_reset();
}

static void spi_rx_dma_loop_start(uint8_t flush_fifo)
{
    uint32_t lock;
    SYS_DisableAllIrq(&lock);
    PFIC_DisableIRQ(SPI0_IRQn);
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
        spi_rx_dma_state_reset();
        s_spi_rx_total_bytes = 0u;
    }

    R32_SPI0_DMA_BEG = (uint32_t)s_spi_rx_dma_buf;
    R32_SPI0_DMA_END = (uint32_t)(s_spi_rx_dma_buf + SPI_RX_DMA_BUF_SIZE);
    R32_SPI0_DMA_NOW = (uint32_t)s_spi_rx_dma_buf;
    /* Event reads and overflow recovery restart the RX DMA at offset zero.
     * NSS capture must use the same epoch rather than the old ring offset. */
    s_nss_rx_total=0u;
    R16_SPI0_TOTAL_CNT = SPI_RX_DMA_BUF_SIZE;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END | RB_SPI_IF_FIFO_OV |
                       RB_SPI_IF_FIFO_HF | RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE;
    R8_SPI0_CTRL_CFG |= (uint8_t)(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_OV |
                         SPI0_IT_FIFO_HF | SPI0_IT_BYTE_END | SPI0_IT_FST_BYTE);
    /* One short IRQ per 1024 bytes. RX FIFO errors remain latched for the
     * main-loop recovery; no frame parsing or diagnostic work in this ISR. */
    SPI0_ITCfg(ENABLE, SPI0_IT_DMA_END);
    PFIC_EnableIRQ(SPI0_IRQn);
    SYS_RecoverIrq(lock);
}

static void spi_rx_restart_after_tx(void)
{
    PFIC_DisableIRQ(SPI0_IRQn);
    spi_rx_dma_loop_start(1u);
}

static void spi_tx_finish(uint8_t timed_out)
{
    uint32_t lock;SYS_DisableAllIrq(&lock);
    if(s_spi_tx_pending) {
        s_spi_tx_pending=0u;
        if(timed_out)++s_spi_tx_recover_count;else ++s_spi_tx_done_count;
        spi_rx_restart_after_tx();
        rfm_spi_port_set_irq(false);
    }
    SYS_RecoverIrq(lock);
}

void rfm_spi_port_init(void)
{
    uint8_t i;

    PFIC_DisableIRQ(GPIO_A_IRQn);
    PFIC_DisableIRQ(SPI0_IRQn);
    /* v16 used the reset priority (0). Input capture and CNT_END reply
     * completion must retain that priority over the 0x80 RF timer. Changing
     * this contract coincided with the capture-enabled regression in v17;
     * restore the working baseline before considering scheduling changes. */
    PFIC_SetPriority(GPIO_A_IRQn,0x00);
    PFIC_SetPriority(SPI0_IRQn,0x00);
    for(unsigned n=0;n<256u;++n) {
        uint8_t crc=(uint8_t)n;
        for(unsigned bit=0;bit<8u;++bit)
            crc=(uint8_t)((crc<<1)^((crc&0x80u)?0x07u:0u));
        s_spi_input_crc_table[n]=crc;
    }

    rfm_board_latest_ch585_prepare_spi_pins();
    rfm_board_latest_ch585_set_w_int(false);

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
    s_spi_rx_input_end_valid = 0u;
    s_spi_rx_input_end = 0u;
    s_spi_rx_latest_valid = 0u;
    s_spi_rx_latest_gen = 0u;
    for(i = 0u; i < RFM_RF_INPUT_PAYLOAD_LEN; ++i)
    {
        s_spi_rx_latest_payload[i] = 0u;
    }

    spi_rx_dma_loop_start(1u);
    /* Input delivery is independent of the capture switch. NSS handles only
     * the completed input frame; sidecars and commands stay in the main loop. */
    R16_PA_INT_EN &= (uint16_t)~SPI_WAKE_PIN;
    GPIOA_ClearITFlagBit(SPI_WAKE_PIN);
    GPIOA_ClearITFlagBit(RFM_BOARD_SPI_NSS_PIN);
    GPIOA_ITModeCfg(RFM_BOARD_SPI_NSS_PIN,GPIO_ITMode_RiseEdge);
    PFIC_EnableIRQ(GPIO_A_IRQn);
    /*
     * W_INT is active-low on the latest board.  Signal READY only after the
     * RF board port can receive the first command, and only once per cold
     * boot.  Reinitialization after RF sleep must not look like another boot.
     */
    if(s_board_boot_ready_sent == 0u)
    {
        s_board_boot_ready_sent = 1u;
        rfm_board_latest_ch585_pulse_boot_ready();
    }
}

static void clear_wake_it_flag(void)
{
    R16_PA_INT_IF = (uint16_t)SPI_WAKE_PIN;
}

static void clear_wake_pending(void)
{
    clear_wake_it_flag();
    PFIC_ClearPendingIRQ(GPIO_A_IRQn);
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
    if(!rfm_board_latest_ch585_wake_high())
    {
        s_sleep_block_flags |= 0x04u;
    }
    if((s_spi_tx_pending != 0u) ||
       (s_spi_control_count != 0u) ||
       !rfm_board_latest_ch585_wake_high())
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
    s_sleep_rx_quiesced=1u;
    R16_PA_INT_EN &= (uint16_t)~RFM_BOARD_SPI_NSS_PIN;
    GPIOA_ClearITFlagBit(RFM_BOARD_SPI_NSS_PIN);
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

    rfm_board_latest_ch585_prepare_sleep_pins();
#if (SPI_WAKE_USE_INTX != 0u)
    GPIOPinRemap(ENABLE, RB_PIN_INTX);
#endif

    s_wake_irq_count = 0u;
    clear_wake_pending();
    SPI_PORT_LOG("SLEEP_PREP pa15=%u mode=sleep_falledge",
                 rfm_board_latest_ch585_wake_high() ? 1u : 0u);
    if(!rfm_board_latest_ch585_wake_high())
    {
#if (SPI_WAKE_USE_INTX != 0u)
        GPIOPinRemap(DISABLE, RB_PIN_INTX);
#endif
        rfm_spi_port_init();
        return;
    }
    GPIOA_ITModeCfg(SPI_WAKE_PIN, GPIO_ITMode_FallEdge);
    clear_wake_pending();
    PWR_PeriphWakeUpCfg(ENABLE, RB_SLP_GPIO_WAKE | RB_GPIO_EDGE_WAKE, Long_Delay);

    SPI_PORT_LOG("SLEEP_ENTER_SLEEP pa15=%u irq_count=%lu",
                 rfm_board_latest_ch585_wake_high() ? 1u : 0u,
                 (uint32_t)s_wake_irq_count);
    SPI_PORT_LOG_FLUSH();
    DelayMs(100);
    if(!rfm_board_latest_ch585_wake_high())
    {
        SPI_PORT_LOG("SLEEP_ABORT_WAKE_LOW");
        PWR_PeriphWakeUpCfg(DISABLE, RB_SLP_GPIO_WAKE | RB_GPIO_EDGE_WAKE, Long_Delay);
#if (SPI_WAKE_USE_INTX != 0u)
        GPIOPinRemap(DISABLE, RB_PIN_INTX);
#endif
        rfm_spi_port_init();
        return;
    }
    s_wake_irq_count = 0u;
    clear_wake_pending();
    PFIC_EnableIRQ(GPIO_A_IRQn);
    LowPower_Sleep(RB_PWR_RAM32K | RB_PWR_RAM96K | RB_PWR_EXTEND);

    SetSysClock(SYSCLK_FREQ);
    DelayUs(300);
    SPI_PORT_LOG("WAKE_RETURN_SLEEP pa15=%u irq_count=%lu",
                 rfm_board_latest_ch585_wake_high() ? 1u : 0u,
                 (uint32_t)s_wake_irq_count);

    RFIP_WakeUpRegInit();
    PFIC_DisableIRQ(GPIO_A_IRQn);
    clear_wake_pending();
    PWR_PeriphWakeUpCfg(DISABLE, RB_SLP_GPIO_WAKE | RB_GPIO_EDGE_WAKE, Long_Delay);
#if (SPI_WAKE_USE_INTX != 0u)
    GPIOPinRemap(DISABLE, RB_PIN_INTX);
#endif
    rfm_spi_port_init();
}

static volatile uint32_t s_measure_end[256];
static volatile uint8_t s_measure_valid[256];
static volatile uint8_t s_measure_tag[256];
void rfm_spi_port_measure_enable(uint8_t enable) {
    if(rfm_board_latest_ch585_usb_spi_owner()) return;
    if(s_measure_nss==enable)return;
    s_measure_nss=enable;
    s_measure_diag=0;
    memset((void*)s_measure_valid,0,sizeof(s_measure_valid));
    /* Do not change NSS delivery: normal input uses it with capture off too. */
}
__HIGH_CODE
uint8_t rfm_spi_port_input_end(uint8_t tag,uint8_t seq,uint32_t* cycles) {
    uint32_t t=s_measure_end[seq];
    if(!s_measure_valid[seq] || s_measure_tag[seq]!=tag ||
       (uint32_t)(SysTick->CNT-t)>GetSysClock()/125u)return 0;
    *cycles=t;return 1;
}
uint8_t rfm_spi_port_measure_diag(void) {return (s_measure_nss ? 1u : 0u)|s_measure_diag;}
__INTERRUPT
__HIGH_CODE
void GPIOA_IRQHandler(void)
{
    if(rfm_board_latest_ch585_usb_spi_owner())
    {
        const uint16_t flags = GPIOA_ReadITFlagPort();
        if((flags & RFM_BOARD_SPI_NSS_PIN) != 0u)
        {
            GPIOA_ClearITFlagBit(RFM_BOARD_SPI_NSS_PIN);
            usb_board_link_port_nss_rise_irq_handler();
        }
        if((flags & (uint16_t)~RFM_BOARD_SPI_NSS_PIN) != 0u)
        {
            GPIOA_ClearITFlagBit(
                (uint16_t)(flags & (uint16_t)~RFM_BOARD_SPI_NSS_PIN));
        }
        return;
    }

    const uint16_t flags=GPIOA_ReadITFlagPort();
    if(flags & (uint16_t)~RFM_BOARD_SPI_NSS_PIN)
        GPIOA_ClearITFlagBit(flags & (uint16_t)~RFM_BOARD_SPI_NSS_PIN);
    if(flags & RFM_BOARD_SPI_NSS_PIN) {
        const uint32_t now=SysTick->CNT;
        GPIOA_ClearITFlagBit(RFM_BOARD_SPI_NSS_PIN);
        if(s_spi_tx_pending || s_sleep_rx_quiesced)return;
        const uint32_t end=spi_rx_dma_produced(), start=s_nss_rx_total;
        s_nss_rx_total=end;
        const uint32_t n=end-start;
        if(s_measure_nss)s_measure_diag|=2u;
        if(n==14u || n==38u) {
            uint8_t frame[14],sum=0;
            for(unsigned i=0;i<sizeof(frame);++i)
                frame[i]=s_spi_rx_dma_buf[(start+i)%SPI_RX_DMA_BUF_SIZE];
            if((uint32_t)(spi_rx_dma_produced()-start)>=SPI_RX_DMA_BUF_SIZE ||
               (R8_SPI0_INT_FLAG & RB_SPI_IF_FIFO_OV))return;
            if(frame[0]!=RFM_SPI_SYNC || frame[1]!=SPI_INPUT_CMD ||
               frame[2]!=RFM_RF_INPUT_PAYLOAD_LEN)return;
            for(unsigned i=0;i<13u;++i)sum+=frame[i];
            if(sum!=frame[13] || !spi_input_crc_valid(frame+3))return;
            if(s_measure_nss) {
                const uint8_t seq=frame[3];
                s_measure_tag[seq]=frame[7]>>2;
                s_measure_end[seq]=now;s_measure_valid[seq]=1;
                s_measure_diag|=4u;
            }
            /* Fixed 14-byte validated input only. No sidecar parsing,
             * RF launch, battery processing or control replies in this ISR. */
            spi_rx_accept_input(frame+3,start+sizeof(frame));
        }
        return;
    }
    s_wake_irq_count++;
    clear_wake_pending();
    if(s_sleep_rx_quiesced)PFIC_DisableIRQ(GPIO_A_IRQn);
}

void rfm_spi_port_set_irq(bool asserted)
{
    rfm_board_latest_ch585_set_w_int(asserted);
}

void rfm_spi_port_service(void)
{
    if(s_spi_tx_pending != 0u)
    {
        if((R8_SPI0_INT_FLAG & RB_SPI_IF_CNT_END) != 0u)
        {
            spi_tx_finish(0u);
        }
        else if(rfm_board_latest_ch585_nss_high() &&
                ((int32_t)(spi_now_us() - (s_spi_tx_start_us + SPI_TX_PENDING_RECOVER_US)) >= 0))
        {
            spi_tx_finish(1u);
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
        s_spi_rx_dma_consumed = spi_rx_dma_produced();
        PFIC_DisableIRQ(SPI0_IRQn);
        R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END | RB_SPI_IF_FIFO_OV |
                           RB_SPI_IF_FIFO_HF | RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE;
        while(R8_SPI0_FIFO_COUNT != 0u)
        {
            (void)R8_SPI0_FIFO;
        }
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

    if(rfm_board_latest_ch585_usb_spi_owner())
    {
        usb_board_link_port_spi_irq_handler();
        return;
    }

    s_spi_rx_isr_count++;
    s_spi_rx_last_flags = flags;

    if(s_spi_tx_pending && (flags & RB_SPI_IF_CNT_END)) {
        // Physical final byte consumed. Restore RX before deasserting ready;
        // never make the master wait for another main-loop FIFO service.
        spi_tx_finish(0u);
        return;
    }

    if(flags == 0u)
    {
        return;
    }
    if(!s_spi_tx_pending)
    {
        if(flags & RB_SPI_IF_DMA_END) {
            R8_SPI0_INT_FLAG = RB_SPI_IF_DMA_END;
            s_spi_rx_dma_wrap_bytes += SPI_RX_DMA_BUF_SIZE;
        }
        if(flags & RB_SPI_IF_CNT_END)R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
        /* Leave FIFO_OV for main-loop recovery; clearing it here would let
         * the parser splice bytes across a lost part of the input stream. */
        return;
    }

    R8_SPI0_INT_FLAG = flags;
    s_spi_rx_bad_irq_count++;
}

bool rfm_spi_port_runtime_reply_ready(void)
{
    /* Called only after the main loop drains parsed control frames. Do not
     * reset DMA while a master transaction or unconsumed RX data exists. */
    return s_spi_tx_pending == 0u && rfm_board_latest_ch585_nss_high() &&
           s_spi_control_count == 0u && spi_rx_dma_produced() == s_spi_rx_dma_consumed;
}

bool rfm_spi_port_try_write(const uint8_t *buf, size_t len)
{
    size_t i;
    uint32_t lock;

    if((buf == 0) || (len == 0u) || (len > 4095u) || (len > sizeof(s_spi_tx_buf)))
    {
        return false;
    }
    if(!rfm_spi_port_runtime_reply_ready())
    {
        return false;
    }

    /* Only main context produces replies. Stage bytes while RX stays live,
     * then recheck ownership before changing direction. */
    for(i = 0u; i < len; ++i)
    {
        s_spi_tx_buf[i] = buf[i];
    }
    SYS_DisableAllIrq(&lock);
    if(!rfm_spi_port_runtime_reply_ready())
    {
        SYS_RecoverIrq(lock);
        return false;
    }

    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_OV);
    PFIC_DisableIRQ(SPI0_IRQn);
    R8_SPI0_CTRL_CFG &= (uint8_t)(~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP));
    R8_SPI0_CTRL_MOD &= (uint8_t)(~RB_SPI_FIFO_DIR);
    R16_SPI0_TOTAL_CNT = (uint16_t)len;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END | RB_SPI_IF_FIFO_OV;

    while(R8_SPI0_FIFO_COUNT != 0u)
    {
        (void)R8_SPI0_FIFO;
    }

    s_spi_tx_len = (uint16_t)len;
    s_spi_tx_pos = (uint16_t)len;
    R32_SPI0_DMA_BEG = (uint32_t)s_spi_tx_buf;
    R32_SPI0_DMA_END = (uint32_t)(s_spi_tx_buf+len);
    R32_SPI0_DMA_NOW = (uint32_t)s_spi_tx_buf;
    s_spi_tx_pending = 1u;
    s_spi_tx_start_us = spi_now_us();
    // Nonblocking form of SDK SPI0_SlaveDMATrans. DMA owns every byte;
    // do not mix software FIFO writes with DMA or stop at DMA_END (prefetch).
    R8_SPI0_CTRL_CFG |= RB_SPI_DMA_ENABLE;
    /* Publish ready BEFORE enabling completion. A master input already in
     * flight can consume a short reply immediately. If a caller asserted
     * W_INT after this function returned, CNT_END could first restore RX
     * and clear ready, then the caller would leave a phantom ready asserted
     * forever with tx_pending == 0. All reply producers use this ownership. */
    rfm_board_latest_ch585_set_w_int(true);
    SPI0_ITCfg(ENABLE, SPI0_IT_CNT_END);
    PFIC_EnableIRQ(SPI0_IRQn);
    SYS_RecoverIrq(lock);
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
