#include "usb_webhid_memory.h"
#include "usb_board_link.h"
#include "usb_webhid_fast.h"
#include "webhid_fast_link.h"
#include "usb_board_link_port_ch585.h"

#include <string.h>

#include "CH58x_common.h"
#include "board_latest_ch585.h"

#define USB_SPI_PINS (GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15)
#define USB_SPI_NSS_PIN GPIO_Pin_12
#define USB_SPI_IRQ_PIN GPIO_Pin_5
#define USB_SPI_RELEASE_GAP_US 1000u
#define USB_SPI_ALL_FLAGS (RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END | \
                           RB_SPI_IF_FIFO_OV | RB_SPI_IF_FIFO_HF | \
                           RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE)

static usb_spi_rx_ring_t s_rx_ring;
__attribute__((aligned(4)))
static uint8_t s_rx_dma[USB_SPI_RX_DMA_BYTES];
static uint32_t s_rx_dma_last_pos;
static volatile uint32_t s_rx_dma_wrap_bytes;
static uint32_t s_rx_dma_consumed;
static uint8_t s_tx_frames[USB_SPI_TX_SLOTS][USB_BOARD_LINK_MAX_FRAME_BYTES];
static uint8_t s_tx_large[WHF_BLOCK_BYTES] __attribute__((aligned(4)));
static uint8_t s_tx_large_owned;
static uint8_t *tx_slot(uint8_t slot);
static uint16_t s_tx_lengths[USB_SPI_TX_SLOTS];
static uint8_t *tx_slot(uint8_t slot) {
    return s_tx_lengths[slot] > USB_BOARD_LINK_MAX_FRAME_BYTES ? s_tx_large : s_tx_frames[slot];
}
static uint8_t s_tx_head;
static volatile uint8_t s_tx_tail;
static volatile uint8_t s_tx_count;
static volatile uint8_t s_ready;
static volatile uint8_t s_tx_armed;
static volatile uint8_t s_tx_nss_seen;
static volatile uint8_t s_release_gap_pending;
static volatile uint32_t s_release_gap_started_cycles;
static uint32_t s_release_gap_cycles_per_us;
static volatile uint8_t s_port_fault;
static volatile uint8_t s_fast_input;
static volatile uint8_t s_fast_webhid;
static uint32_t s_input_fault_detail[3];

static void record_overflow(uint8_t cause, uint32_t produced, uint32_t consumed)
{
    uint32_t detail = cause | ((uint32_t)R8_SPI0_INT_FLAG << 8u) |
        ((uint32_t)R8_SPI0_FIFO_COUNT << 16u) | ((uint32_t)s_tx_armed << 24u);
    usb_webhid_fast_port_detail(detail, produced, consumed);
    if(s_fast_input && !s_input_fault_detail[0]) {
        s_input_fault_detail[0]=detail;
        s_input_fault_detail[1]=produced;
        s_input_fault_detail[2]=consumed;
    }
    s_port_fault = USB_BOARD_STATUS_QUEUE_FULL;
}

static uint8_t nss_is_high(void)
{
    return (GPIOA_ReadPortPin(USB_SPI_NSS_PIN) != 0u) ? 1u : 0u;
}

static void spi_fifo_clear(void)
{
    R8_SPI0_CTRL_MOD |= RB_SPI_ALL_CLEAR;
    R8_SPI0_CTRL_MOD &= (uint8_t)~RB_SPI_ALL_CLEAR;
}

static USB_WEBHID_RAM void port_lock(void)
{
    PFIC_DisableIRQ(SPI0_IRQn);
    PFIC_DisableIRQ(GPIO_A_IRQn);
}

static USB_WEBHID_RAM void port_unlock(void)
{
    /*
     * shutdown() intentionally leaves both shared IRQs masked.  Late parser
     * polling or a stale queue caller must not re-enable them after the USB
     * role has released SPI0/GPIOA ownership.
     */
    if(s_ready == 0u)
    {
        return;
    }
    PFIC_EnableIRQ(GPIO_A_IRQn);
    PFIC_EnableIRQ(SPI0_IRQn);
}

static void rx_fifo_start(void)
{
    R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    SPI0_ITCfg(DISABLE,
               SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_HF |
               SPI0_IT_FIFO_OV);
    spi_fifo_clear();
    R8_SPI0_CTRL_MOD = (uint8_t)((R8_SPI0_CTRL_MOD | RB_SPI_FIFO_DIR) &
                                 (uint8_t)~RB_SPI_SLV_CMD_MOD);
    R8_SPI0_INT_FLAG = USB_SPI_ALL_FLAGS;
    /* Drain every half FIFO in the IRQ. The polling selector already proves
     * this byte-stream mode on the PCB, and it removes any dependency on a
     * PA12/NSS edge being retained by the GPIO peripheral. */
    SPI0_ITCfg(ENABLE, SPI0_IT_FIFO_HF | SPI0_IT_FIFO_OV);
}

static USB_WEBHID_RAM uint32_t rx_dma_position(void)
{
    return usb_spi_rx_dma_position(R32_SPI0_DMA_NOW, (uint32_t)s_rx_dma,
                                   s_rx_dma_last_pos);
}

/* Bootstrap/IAP timing remains unchanged. Application input and committed
 * WebConfig use bounded SRAM copies. */
static void *port_copy(void *destination, const void *source, size_t length)
{
    if(s_fast_input || (s_fast_webhid && usb_webhid_fast_ready()))
        return usb_webhid_copy(destination,source,length);
    return memcpy(destination,source,length);
}

static bool rx_dma_wrap_pending(void *context)
{
    (void)context;
    return (R8_SPI0_INT_FLAG & RB_SPI_IF_DMA_END) != 0u;
}
static void rx_dma_clear_wrap(void *context)
{
    (void)context;
    R8_SPI0_INT_FLAG = RB_SPI_IF_DMA_END;
}
static uint32_t rx_dma_read_position(void *context)
{
    (void)context;
    return rx_dma_position();
}

static void rx_push_block(const uint8_t *data, uint16_t length)
{
    uint16_t first;

    if((data == 0) || (length == 0u))
    {
        return;
    }
    if(length > (uint16_t)(USB_SPI_RX_FIFO_BYTES - s_rx_ring.count))
    {
        /*
         * Preserve all complete data already queued for the parser. Dropping
         * a whole DMA suffix is explicit and recoverable at the next 0x5A
         * sync; it cannot silently splice two valid frames.
         */
        record_overflow(5u, length, s_rx_ring.count);
        return;
    }

    first = (uint16_t)(USB_SPI_RX_FIFO_BYTES - s_rx_ring.head);
    if(first > length)
    {
        first = length;
    }
    port_copy(&s_rx_ring.data[s_rx_ring.head], data, first);
    if(length > first)
    {
        port_copy(&s_rx_ring.data[0], &data[first],
               (uint16_t)(length - first));
    }
    s_rx_ring.head = (uint16_t)(s_rx_ring.head + length);
    if(s_rx_ring.head >= USB_SPI_RX_FIFO_BYTES)
    {
        s_rx_ring.head =
            (uint16_t)(s_rx_ring.head - USB_SPI_RX_FIFO_BYTES);
    }
    s_rx_ring.count = (uint16_t)(s_rx_ring.count + length);
}

static void rx_dma_start(uint8_t reset_buffer)
{
    /* DMA_NOW bounds every published byte. Clearing all 4092 bytes on every
     * direction change is unnecessary and delays the ready acknowledgement
     * with IRQs masked; previous contents are never part of the new delta. */
    (void)reset_buffer;
    R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    SPI0_ITCfg(DISABLE,
               SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_HF |
               SPI0_IT_FIFO_OV);
    spi_fifo_clear();
    R8_SPI0_CTRL_MOD = (uint8_t)((R8_SPI0_CTRL_MOD | RB_SPI_FIFO_DIR) &
                                 (uint8_t)~RB_SPI_SLV_CMD_MOD);
    s_rx_dma_last_pos = 0u;
    s_rx_dma_wrap_bytes = s_rx_dma_consumed = 0u;
    R32_SPI0_DMA_BEG = (uint32_t)s_rx_dma;
    R32_SPI0_DMA_END = (uint32_t)(s_rx_dma + USB_SPI_RX_DMA_BYTES);
    R32_SPI0_DMA_NOW = (uint32_t)s_rx_dma;
    R16_SPI0_TOTAL_CNT = USB_SPI_RX_DMA_BYTES;
    R8_SPI0_INT_FLAG = USB_SPI_ALL_FLAGS;
    R8_SPI0_CTRL_CFG |= (uint8_t)(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    /* Account each ring epoch even while the parser handles a large block. */
    SPI0_ITCfg(ENABLE, SPI0_IT_DMA_END);
}

static USB_WEBHID_RAM void rx_dma_collect_locked(void)
{
    uint8_t flags;
    uint32_t position;
    uint32_t produced;
    uint32_t delta;
    uint32_t first;
    uint32_t irq_status;

    if((s_fast_input == 0u && s_fast_webhid == 0u))
    {
        return;
    }
    flags = R8_SPI0_INT_FLAG;
    if((flags & RB_SPI_IF_FIFO_OV) != 0u)
    {
        record_overflow(1u, s_rx_dma_wrap_bytes + rx_dma_position(), s_rx_dma_consumed);
        R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
        rx_dma_start(1u);
        return;
    }
    /* CNT_END measures transferred bytes, not a DMA ring epoch. Using it as
     * a wrap can republish old ciphertext, whose embedded 0x5B then looks
     * like a malformed block header. Recheck DMA_END after sampling NOW. */
    if((flags & RB_SPI_IF_CNT_END) != 0u) R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
    /* NSS may call this from an ISR. Mask all IRQs for the short register
     * snapshot so the SPI ISR cannot consume DMA_END between epoch/NOW. */
    SYS_DisableAllIrq(&irq_status);
    produced = usb_spi_rx_dma_produced(&s_rx_dma_wrap_bytes, 0,
        rx_dma_wrap_pending, rx_dma_clear_wrap, rx_dma_read_position);
    position = produced - s_rx_dma_wrap_bytes;
    SYS_RecoverIrq(irq_status);
    delta = produced - s_rx_dma_consumed;
    if(delta == 0u)
    {
        return;
    }
    if(delta > USB_SPI_RX_DMA_BYTES ||
       delta > (uint32_t)(USB_SPI_RX_FIFO_BYTES - s_rx_ring.count))
    {
        /* Drop the whole DMA delta; never splice a wrapped suffix into a
         * previously complete frame stream. The parser will resynchronize on
         * the next 0x5A after the reported overflow. */
        record_overflow(delta > USB_SPI_RX_DMA_BYTES ? 2u : 3u, produced, s_rx_dma_consumed);
        s_rx_dma_last_pos = position;
        s_rx_dma_consumed = produced;
        return;
    }
    first = USB_SPI_RX_DMA_BYTES - s_rx_dma_last_pos;
    if(first > delta)
    {
        first = delta;
    }
    rx_push_block(&s_rx_dma[s_rx_dma_last_pos], (uint16_t)first);
    if(delta > first)
    {
        rx_push_block(s_rx_dma, (uint16_t)(delta - first));
    }
    s_rx_dma_last_pos = position;
    s_rx_dma_consumed = produced;
}

static void rx_backend_start(uint8_t reset_buffer)
{
    if((s_fast_input != 0u || s_fast_webhid != 0u))
    {
        rx_dma_start(reset_buffer);
    }
    else
    {
        rx_fifo_start();
    }
}

static void rx_drain_fifo_locked(void)
{
    while(R8_SPI0_FIFO_COUNT != 0u)
    {
        const uint8_t byte = R8_SPI0_FIFO;
        rx_push_block(&byte, 1u);
    }
}

static void service_pending_nss_rise_locked(void)
{
    /* Preserve every received byte before the RX backend is repurposed. */
    if((s_fast_input != 0u || s_fast_webhid != 0u))
    {
        rx_dma_collect_locked();
    }
    else
    {
        rx_drain_fifo_locked();
    }
    GPIOA_ClearITFlagBit(USB_SPI_NSS_PIN);
}

static bool tx_dma_arm_locked(void)
{
    const uint16_t length = s_tx_lengths[s_tx_tail];
    uint32_t irq_status;

    /*
     * Install the complete TX DMA while NSS and W_INT are both idle.  The
     * STM32 write path holds NSS low for an ownership guard before clocking,
     * so a concurrent writer that wins the race is detected by the second
     * NSS sample and RX is restored before that guard expires.
     */
    if(nss_is_high() == 0u)
    {
        return false;
    }

    /*
     * USB2_DEVICE_IRQHandler may copy a 512-byte packet and can run longer
     * than the STM32 ownership guard.  Make the complete RX->TX commit
     * non-preemptible: a command writer then either owns NSS before the
     * switch, or observes W_INT low after a fully armed TX path.
     */
    SYS_DisableAllIrq(&irq_status);
    service_pending_nss_rise_locked();
    if(nss_is_high() == 0u)
    {
        SYS_RecoverIrq(irq_status);
        return false;
    }

    R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_DMA_END);
    spi_fifo_clear();
    R8_SPI0_CTRL_MOD = (uint8_t)(R8_SPI0_CTRL_MOD &
                                 (uint8_t)~(RB_SPI_FIFO_DIR |
                                            RB_SPI_SLV_CMD_MOD));
    R32_SPI0_DMA_BEG = (uint32_t)tx_slot(s_tx_tail);
    R32_SPI0_DMA_END = (uint32_t)(tx_slot(s_tx_tail) + length);
    R32_SPI0_DMA_NOW = (uint32_t)tx_slot(s_tx_tail);
    R16_SPI0_TOTAL_CNT = length;
    R8_SPI0_INT_FLAG = USB_SPI_ALL_FLAGS;
    R8_SPI0_CTRL_CFG |= RB_SPI_DMA_ENABLE;
    /*
     * DMA_END only means that DMA has filled the hardware FIFO.  It can
     * precede the master's first clock for short frames, so only CNT_END is
     * allowed to retire a transmitted event.
     */
    SPI0_ITCfg(ENABLE, SPI0_IT_CNT_END | SPI0_IT_FIFO_OV);
    __asm volatile("fence iorw, iorw" ::: "memory");
    if(nss_is_high() == 0u)
    {
        /*
         * A command writer asserted NSS during the short configuration
         * window.  It has not clocked yet because of the STM32 ownership
        * guard; restore RX and let that transaction proceed.
         */
        rx_backend_start(1u);
        SYS_RecoverIrq(irq_status);
        return false;
    }
    s_tx_nss_seen = 0u;
    s_tx_armed = 1u;
    __asm volatile("fence iorw, iorw" ::: "memory");
    /* Advertising the event is the final operation after TX is ready. */
    GPIOA_ResetBits(USB_SPI_IRQ_PIN);
    __asm volatile("fence iorw, iorw" ::: "memory");
    SYS_RecoverIrq(irq_status);
    return true;
}

static void tx_dma_finish(void)
{
    const uint8_t flags = R8_SPI0_INT_FLAG;
    const uint8_t complete =
        ((flags & RB_SPI_IF_CNT_END) != 0u) ? 1u : 0u;

    /* The poller's NSS sample can precede the master's first clock (or a
     * USB interrupt). FST_BYTE observed afterwards belongs to an in-flight
     * read, not necessarily an NSS release. Recheck at the retirement point
     * before touching DMA/FIFO; CNT_END alone also cannot release ownership. */
    if(nss_is_high() == 0u)
    {
        s_tx_nss_seen = 1u;
        return;
    }

    /* A WebHID writer can lose W_INT arbitration after asserting NSS and
     * release it without clocking a byte. That is not an aborted event read.
     * Keep the queued event, preloaded DMA/FIFO and W_INT ownership intact so
     * the master can read it next. FST_BYTE stays latched throughout TX (see
     * the IRQ handler); a genuinely partial transfer must still fault below.
     * DMA_END alone is only FIFO prefill, not evidence of master clocks. */
    if(s_fast_webhid && !complete && !(flags & RB_SPI_IF_FST_BYTE) &&
       R16_SPI0_TOTAL_CNT == s_tx_lengths[s_tx_tail])
    {
        s_tx_nss_seen = 0u;
        return;
    }

    if(!complete && s_fast_webhid)
    {
        /* Preserve the first truncated-read evidence before clearing the
         * FIFO/counter. Feature report detail carries expected/remaining. */
        usb_webhid_fast_port_detail(7u | ((uint32_t)flags << 8u) |
            ((uint32_t)R8_SPI0_FIFO_COUNT << 16u) | ((uint32_t)s_tx_armed << 24u),
            s_tx_lengths[s_tx_tail], R16_SPI0_TOTAL_CNT);
    }
    R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_DMA_END);
    spi_fifo_clear();
    if(complete != 0u)
    {
        if(s_tx_lengths[s_tx_tail] > USB_BOARD_LINK_MAX_FRAME_BYTES) s_tx_large_owned=0u;
        ++s_tx_tail;
        if(s_tx_tail >= USB_SPI_TX_SLOTS)
        {
            s_tx_tail = 0u;
        }
        --s_tx_count;
    }
    else
    {
        /*
         * NSS returned high before the complete frame was shifted.  Keep the
         * frame at the queue tail for a clean retry and surface the aborted
         * transfer rather than silently consuming it.
         */
        s_port_fault = USB_BOARD_STATUS_INTERNAL_ERROR;
    }
    s_tx_armed = 0u;
    s_tx_nss_seen = 0u;
    rx_backend_start(1u);
    __asm volatile("fence iorw, iorw" ::: "memory");
    /* W_INT high is the invariant that RX DMA is fully ready for a write. */
    GPIOA_SetBits(USB_SPI_IRQ_PIN);
    s_release_gap_started_cycles = SysTick->CNTL;
    s_release_gap_pending = 1u;
}

bool usb_board_link_port_init(void)
{
    GPIOPinRemap(DISABLE, RB_PIN_SPI0);
    GPIOADigitalCfg(ENABLE, USB_SPI_PINS | USB_SPI_IRQ_PIN);
    GPIOA_ModeCfg(GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_15, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(USB_SPI_IRQ_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_SetBits(USB_SPI_IRQ_PIN);

    SPI0_SlaveInit();
    port_lock();
    SPI0_ITCfg(DISABLE,
               SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_OV);
    R16_PA_INT_EN &= (uint16_t)~USB_SPI_NSS_PIN;
    GPIOA_ClearITFlagBit(USB_SPI_NSS_PIN);
    s_tx_large_owned=0u;
    usb_spi_rx_ring_reset(&s_rx_ring);
    s_tx_head = 0u;
    s_tx_tail = 0u;
    s_tx_count = 0u;
    s_tx_armed = 0u;
    s_tx_nss_seen = 0u;
    s_release_gap_pending = 0u;
    s_release_gap_started_cycles = 0u;
    s_release_gap_cycles_per_us = GetSysClock() / 1000000u;
    s_port_fault = USB_BOARD_STATUS_OK;
    s_fast_input = 0u;
    s_fast_webhid = 0u;
    s_rx_dma_last_pos = 0u;
    memset(s_tx_lengths, 0, sizeof(s_tx_lengths));
    rx_fifo_start();
    s_ready = 1u;
    GPIOA_ITModeCfg(USB_SPI_NSS_PIN, GPIO_ITMode_RiseEdge);
    GPIOA_ClearITFlagBit(USB_SPI_NSS_PIN);
    port_unlock();
    return true;
}

void usb_board_link_port_shutdown(void)
{
    s_ready = 0u;
    port_lock();
    SPI0_ITCfg(DISABLE,
               SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_OV);
    R16_PA_INT_EN &= (uint16_t)~USB_SPI_NSS_PIN;
    GPIOA_ClearITFlagBit(USB_SPI_NSS_PIN);
    s_tx_armed = 0u;
    s_tx_nss_seen = 0u;
    s_release_gap_pending = 0u;
    R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    spi_fifo_clear();
    GPIOA_SetBits(USB_SPI_IRQ_PIN);
    /* Deliberately leave both IRQs disabled after shutdown. */
}

USB_WEBHID_RAM void usb_board_link_port_process(void)
{
    uint8_t nss_high;

    if(s_ready == 0u)
    {
        return;
    }

    if(((s_fast_input != 0u || s_fast_webhid != 0u)) && (s_tx_armed == 0u))
    {
        port_lock();
        rx_dma_collect_locked();
        port_unlock();
    }

    if(s_release_gap_pending != 0u)
    {
        /*
         * Make the high level observable by STM32 before another queued event
         * reclaims W_INT. RX DMA and the NSS rising-edge ISR remain active
         * during this gap, so incoming commands are not stalled.
         */
        /* PREPARE is acknowledged while the master still uses bootstrap
         * timing. Shorten the release only after the high-speed probe has
         * been committed by both peers, never at the RX-backend switch. */
        const uint32_t gap_cycles = s_release_gap_cycles_per_us *
            (s_fast_webhid && usb_webhid_fast_ready() ? 20u : USB_SPI_RELEASE_GAP_US);
        /* Count from W_INT release, without stopping parsing/EP1 service in
         * the outer loop. Unsigned subtraction also handles CNTL wrap. */
        port_lock();
        if((s_release_gap_pending != 0u) &&
           ((uint32_t)(SysTick->CNTL - s_release_gap_started_cycles) >= gap_cycles))
        {
            s_release_gap_pending = 0u;
        }
        port_unlock();
        return;
    }

    nss_high = nss_is_high();
    if(s_tx_armed != 0u)
    {
        if(nss_high == 0u)
        {
            s_tx_nss_seen = 1u;
            return;
        }
        if((s_tx_nss_seen != 0u) ||
           ((R8_SPI0_INT_FLAG & RB_SPI_IF_FST_BYTE) != 0u))
        {
            port_lock();
            if(s_tx_armed != 0u)
            {
                tx_dma_finish();
            }
            port_unlock();
        }
        return;
    }

    /*
     * The release-gap test, TX state, queue state, NSS sample, and arm are
     * one atomic decision against both completion IRQs.  A TX-complete ISR
     * can no longer insert a release request after the test and be bypassed.
     */
    port_lock();
    service_pending_nss_rise_locked();
    if((s_release_gap_pending == 0u) &&
       (s_tx_armed == 0u) &&
       (s_tx_count != 0u) &&
       (nss_is_high() != 0u))
    {
        (void)tx_dma_arm_locked();
    }
    port_unlock();
}

USB_WEBHID_RAM uint16_t usb_board_link_port_read_rx(uint8_t *data, uint16_t capacity)
{
    uint16_t count, first;
    if(!data || !capacity) return 0u;
    port_lock();
    if((s_fast_input || s_fast_webhid) && !s_tx_armed) rx_dma_collect_locked();
    count=s_rx_ring.count<capacity?s_rx_ring.count:capacity;
    first=USB_SPI_RX_FIFO_BYTES-s_rx_ring.tail;
    if(first>count) first=count;
    port_copy(data,s_rx_ring.data+s_rx_ring.tail,first);
    if(count>first) port_copy(data+first,s_rx_ring.data,count-first);
    s_rx_ring.tail=(s_rx_ring.tail+count)%USB_SPI_RX_FIFO_BYTES;
    s_rx_ring.count-=count;
    port_unlock(); return count;
}

bool usb_board_link_port_pop_rx(uint8_t *byte)
{
    bool popped;

    port_lock();
    if(((s_fast_input != 0u || s_fast_webhid != 0u)) && (s_tx_armed == 0u))
    {
        rx_dma_collect_locked();
    }
    popped = usb_spi_rx_ring_pop(&s_rx_ring, byte);
    port_unlock();
    return popped;
}

bool usb_board_link_port_take_fault(uint8_t *fault)
{
    port_lock();
    if((fault == 0) || (s_port_fault == USB_BOARD_STATUS_OK))
    {
        port_unlock();
        return false;
    }
    *fault = s_port_fault;
    s_port_fault = USB_BOARD_STATUS_OK;
    port_unlock();
    return true;
}

bool usb_board_link_port_queue_block(const uint8_t *frame, uint16_t length)
{
    if((frame == 0) || (length < 4u) ||
       (length > WHF_BLOCK_BYTES))
    {
        return false;
    }
    port_lock();
    if(s_tx_count >= USB_SPI_TX_SLOTS ||
       (length>USB_BOARD_LINK_MAX_FRAME_BYTES && s_tx_large_owned))
    {
        port_unlock();
        return false;
    }
    s_tx_lengths[s_tx_head] = length;
    port_copy(tx_slot(s_tx_head), frame, length);
    if(length>USB_BOARD_LINK_MAX_FRAME_BYTES) s_tx_large_owned=1u;
    s_tx_head++;
    if(s_tx_head >= USB_SPI_TX_SLOTS)
    {
        s_tx_head = 0u;
    }
    s_tx_count++;
    port_unlock();
    /*
     * Only the port process may claim W_INT and arm SPI TX.
     */
    return true;
}

bool usb_board_link_port_queue_event(const uint8_t *frame, uint8_t length)
{
    return length <= USB_BOARD_LINK_MAX_FRAME_BYTES && usb_board_link_port_queue_block(frame,length);
}

bool usb_board_link_port_set_fast_webhid(bool enabled)
{
    bool changed=false;
    if(!s_ready || s_fast_input) return false;
    port_lock();
    if(!s_tx_armed && nss_is_high()) {
        service_pending_nss_rise_locked();
        s_fast_webhid=enabled?1u:0u; rx_backend_start(1u); changed=true;
    }
    port_unlock(); return changed;
}

bool usb_board_link_port_set_fast_input(bool enabled)
{
    bool changed = false;

    if(s_ready == 0u)
    {
        return false;
    }
    port_lock();
    if((s_tx_armed == 0u) && (nss_is_high() != 0u))
    {
        service_pending_nss_rise_locked();
        if(enabled && !s_fast_input) memset(s_input_fault_detail,0,sizeof(s_input_fault_detail));
        s_fast_input = enabled ? 1u : 0u;
        rx_backend_start(1u);
        /* Continuous input is collected by the main loop and before TX
         * arbitration. Avoid copying each 14-byte frame in an 8-kHz NSS ISR;
         * DMA_END still counts wraps, and CNT_END retires TX events. */
        if(enabled) R16_PA_INT_EN &= (uint16_t)~USB_SPI_NSS_PIN;
        else GPIOA_ITModeCfg(USB_SPI_NSS_PIN, GPIO_ITMode_RiseEdge);
        changed = true;
    }
    port_unlock();
    return changed;
}

bool usb_board_link_port_is_fast_input(void)
{
    return (s_ready != 0u) && (s_fast_input != 0u);
}

void usb_board_link_port_input_fault_detail(uint32_t detail[3])
{
    port_lock();
    memcpy(detail,s_input_fault_detail,sizeof(s_input_fault_detail));
    port_unlock();
}

void usb_board_link_port_spi_irq_handler(void)
{
    const uint8_t flags = R8_SPI0_INT_FLAG;
    const uint8_t completed = (uint8_t)(flags & RB_SPI_IF_CNT_END);

    if(s_ready == 0u)
    {
        R8_SPI0_INT_FLAG = flags;
        return;
    }
    if((flags & RB_SPI_IF_FIFO_OV) != 0u)
    {
        record_overflow(4u, s_rx_dma_wrap_bytes + rx_dma_position(), s_rx_dma_consumed);
        R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
    }

    if(s_tx_armed != 0u)
    {
        if(completed != 0u)
        {
            /* A zero byte counter alone does not hand the bus back. Keep
             * the FIFO, MISO and DMA buffer owned until the master releases
             * NSS; retiring here while NSS is low can truncate the tail.
             * Mask this level source but retain CNT_END for the main-loop
             * completion check (which also supports input mode without a
             * per-frame NSS interrupt). */
            if(nss_is_high() != 0u)
            {
                tx_dma_finish();
            }
            else
            {
                SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END);
            }
        }
        else if(flags != 0u)
        {
            /* Preserve proof of a started WebHID read until NSS release.
             * Otherwise a later partial read could look like an unclocked
             * ownership handoff after another IRQ acknowledged FST_BYTE. */
            R8_SPI0_INT_FLAG = s_fast_webhid
                ? (uint8_t)(flags & (uint8_t)~RB_SPI_IF_FST_BYTE) : flags;
        }
        return;
    }

    if(s_fast_input != 0u || s_fast_webhid != 0u)
    {
        /* The RX ring epoch must advance before DMA_END is acknowledged.
         * Generic flag clearing here silently lost a wrap and made a cursor
         * such as 164 appear older than the 3200 bytes already consumed. */
        if((flags & RB_SPI_IF_DMA_END) != 0u) {
            R8_SPI0_INT_FLAG = RB_SPI_IF_DMA_END;
            s_rx_dma_wrap_bytes += USB_SPI_RX_DMA_BYTES;
        }
        if((flags & RB_SPI_IF_CNT_END) != 0u)
            R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END;
        return;
    }
    if(((s_fast_input == 0u && s_fast_webhid == 0u)) &&
       ((flags & (RB_SPI_IF_FIFO_HF | RB_SPI_IF_FIFO_OV)) != 0u))
    {
        rx_drain_fifo_locked();
        R8_SPI0_INT_FLAG =
            (uint8_t)(flags & (RB_SPI_IF_FIFO_HF | RB_SPI_IF_FIFO_OV));
    }
    else if(flags != 0u)
    {
        R8_SPI0_INT_FLAG = flags;
    }
}

void usb_board_link_port_nss_rise_irq_handler(void)
{
    if((s_ready == 0u) || (s_tx_armed != 0u))
    {
        return;
    }

    /* Rising NSS is an optional prompt only; FIFO-half IRQs preserve bytes
     * even on units that do not retain this GPIO edge in peripheral mode. */
    if((s_fast_input != 0u || s_fast_webhid != 0u))
    {
        rx_dma_collect_locked();
    }
    else
    {
        rx_drain_fifo_locked();
    }
    if((R8_SPI0_INT_FLAG & RB_SPI_IF_FIFO_OV) != 0u)
    {
        record_overflow(6u, s_rx_dma_wrap_bytes + rx_dma_position(), s_rx_dma_consumed);
    }
}
