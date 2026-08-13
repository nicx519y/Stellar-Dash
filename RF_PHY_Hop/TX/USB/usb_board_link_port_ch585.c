#include "usb_board_link.h"
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
static uint8_t s_tx_frames[USB_SPI_TX_SLOTS][USB_BOARD_LINK_MAX_FRAME_BYTES];
static uint8_t s_tx_lengths[USB_SPI_TX_SLOTS];
static uint8_t s_tx_head;
static volatile uint8_t s_tx_tail;
static volatile uint8_t s_tx_count;
static volatile uint8_t s_ready;
static volatile uint8_t s_tx_armed;
static volatile uint8_t s_tx_nss_seen;
static volatile uint8_t s_release_gap_pending;
static volatile uint8_t s_port_fault;

static uint8_t nss_is_high(void)
{
    return (GPIOA_ReadPortPin(USB_SPI_NSS_PIN) != 0u) ? 1u : 0u;
}

static void spi_fifo_clear(void)
{
    R8_SPI0_CTRL_MOD |= RB_SPI_ALL_CLEAR;
    R8_SPI0_CTRL_MOD &= (uint8_t)~RB_SPI_ALL_CLEAR;
}

static void port_lock(void)
{
    PFIC_DisableIRQ(SPI0_IRQn);
    PFIC_DisableIRQ(GPIO_A_IRQn);
}

static void port_unlock(void)
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
        s_port_fault = USB_BOARD_STATUS_QUEUE_FULL;
        return;
    }

    first = (uint16_t)(USB_SPI_RX_FIFO_BYTES - s_rx_ring.head);
    if(first > length)
    {
        first = length;
    }
    memcpy(&s_rx_ring.data[s_rx_ring.head], data, first);
    if(length > first)
    {
        memcpy(&s_rx_ring.data[0], &data[first],
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
    /* Preserve every received byte before RX FIFO is repurposed for TX. */
    rx_drain_fifo_locked();
    GPIOA_ClearITFlagBit(USB_SPI_NSS_PIN);
}

static bool tx_dma_arm_locked(void)
{
    const uint8_t length = s_tx_lengths[s_tx_tail];
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
    R32_SPI0_DMA_BEG = (uint32_t)s_tx_frames[s_tx_tail];
    R32_SPI0_DMA_END = (uint32_t)(s_tx_frames[s_tx_tail] + length);
    R32_SPI0_DMA_NOW = (uint32_t)s_tx_frames[s_tx_tail];
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
        rx_fifo_start();
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

    R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_DMA_END);
    spi_fifo_clear();
    if(complete != 0u)
    {
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
    rx_fifo_start();
    __asm volatile("fence iorw, iorw" ::: "memory");
    /* W_INT high is the invariant that RX DMA is fully ready for a write. */
    GPIOA_SetBits(USB_SPI_IRQ_PIN);
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
    usb_spi_rx_ring_reset(&s_rx_ring);
    s_tx_head = 0u;
    s_tx_tail = 0u;
    s_tx_count = 0u;
    s_tx_armed = 0u;
    s_tx_nss_seen = 0u;
    s_release_gap_pending = 0u;
    s_port_fault = USB_BOARD_STATUS_OK;
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

void usb_board_link_port_process(void)
{
    uint8_t nss_high;

    if(s_ready == 0u)
    {
        return;
    }

    if(s_release_gap_pending != 0u)
    {
        /*
         * Make the high level observable by STM32 before another queued event
         * reclaims W_INT. RX DMA and the NSS rising-edge ISR remain active
         * during this gap, so incoming commands are not stalled.
         */
        DelayUs(USB_SPI_RELEASE_GAP_US);
        port_lock();
        s_release_gap_pending = 0u;
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

bool usb_board_link_port_pop_rx(uint8_t *byte)
{
    bool popped;

    port_lock();
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

bool usb_board_link_port_queue_event(const uint8_t *frame, uint8_t length)
{
    if((frame == 0) || (length < 4u) ||
       (length > USB_BOARD_LINK_MAX_FRAME_BYTES))
    {
        return false;
    }
    port_lock();
    if(s_tx_count >= USB_SPI_TX_SLOTS)
    {
        port_unlock();
        return false;
    }
    memcpy(s_tx_frames[s_tx_head], frame, length);
    s_tx_lengths[s_tx_head] = length;
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
        s_port_fault = USB_BOARD_STATUS_QUEUE_FULL;
        R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
    }

    if(s_tx_armed != 0u)
    {
        if(completed != 0u)
        {
            tx_dma_finish();
        }
        else if(flags != 0u)
        {
            R8_SPI0_INT_FLAG = flags;
        }
        return;
    }

    if((flags & (RB_SPI_IF_FIFO_HF | RB_SPI_IF_FIFO_OV)) != 0u)
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
    rx_drain_fifo_locked();
    if((R8_SPI0_INT_FLAG & RB_SPI_IF_FIFO_OV) != 0u)
    {
        s_port_fault = USB_BOARD_STATUS_QUEUE_FULL;
    }
}
