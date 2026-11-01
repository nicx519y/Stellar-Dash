/* XORA CH585 USBFS host controller. See third_party/tinyusb/UPSTREAM.md.
 * Only CH585 SDK register definitions are used. Transfers use dedicated DMA
 * bounce buffers, so caller memory need not be aligned or DMA-addressable.
 * IRQ latches completion; bounded main-loop service owns all stack callbacks.
 */
#include "CH58x_common.h"
#include "host/hcd.h"
#include "tusb.h"
#include <string.h>

#define EP_COUNT 32u /* 7 EP0 + 16 HID endpoints + hub + vendor endpoints */
typedef struct
{
    uint8_t used, addr, ep, mps, type, toggle, pid, pending;
    uint8_t interval;
    uint16_t length, done;
    uint8_t *buffer;
    uint32_t started, due, epoch;
} channel_t;
static channel_t channels[EP_COUNT];
static uint8_t rx_dma[64] __attribute__((aligned(4)));
static uint8_t tx_dma[64] __attribute__((aligned(4)));
static int active = -1;
static uint8_t cursor, attached, irq_enabled;
static uint32_t packet_started, next_epoch, drain_started;
static bool draining;
static uint32_t last_cycles, milliseconds, remainder;

uint32_t tusb_time_millis_api(void)
{
    uint32_t now = SysTick->CNTL;
    uint32_t delta = now - last_cycles;
    uint32_t unit = GetSysClock() / 1000u;
    last_cycles = now;
    if (!unit)
        unit = 1;
    milliseconds += delta / unit;
    remainder += delta % unit;
    milliseconds += remainder / unit;
    remainder %= unit;
    return milliseconds;
}
static channel_t *find(uint8_t addr, uint8_t ep)
{
    if (!(ep & 15u))
        ep = 0;
    for (unsigned i = 0; i < EP_COUNT; i++)
        if (channels[i].used && channels[i].addr == addr && channels[i].ep == ep)
            return &channels[i];
    return NULL;
}
static void stop(void)
{
    R8_UH_EP_PID = 0;
    R8_USB_INT_FG = RB_UIF_TRANSFER;
    active = -1;
}
static void finish(channel_t *c, xfer_result_t result)
{
    uint8_t ep = c->ep;
    if (c->pid == USB_PID_IN)
        ep |= 0x80;
    c->pending = 0;
    hcd_event_t event = {.rhport = 0, .dev_addr = c->addr, .event_id = HCD_EVENT_XFER_COMPLETE};
    event.xfer_complete.ep_addr = ep;
    event.xfer_complete.len = c->done;
    event.xfer_complete.result = result;
    event.xfer_complete.epoch = c->epoch;
    hcd_event_handler(&event, false);
}
bool hcd_init(uint8_t port, const tusb_rhport_init_t *init)
{
    (void)port;
    (void)init;
    PFIC_DisableIRQ(USB_IRQn);
    if (!(SysTick->CTLR & SysTick_CTLR_STE))
    {
        SysTick->CTLR = 0;
        SysTick->CNTL = 0;
        SysTick->CMP = SysTick_LOAD_RELOAD_Msk;
        SysTick->CTLR = SysTick_CTLR_STRE | SysTick_CTLR_STCLK | SysTick_CTLR_STE;
    }
    last_cycles = SysTick->CNTL;
    milliseconds = 0;
    remainder = 0;
    memset(channels, 0, sizeof(channels));
    active = -1;
    attached = 0;
    draining = false;
    R16_PIN_CONFIG &= (uint16_t)~RB_PIN_USB_EN;
    R8_USB_CTRL = RB_UC_HOST_MODE;
    R8_UHOST_CTRL = 0;
    R8_USB_DEV_AD = 0;
    R8_UH_EP_MOD = RB_UH_EP_TX_EN | RB_UH_EP_RX_EN;
    R32_UH_RX_DMA = (uint32_t)rx_dma;
    R32_UH_TX_DMA = (uint32_t)tx_dma;
    R8_UH_RX_CTRL = 0;
    R8_UH_TX_CTRL = 0;
    R8_UH_EP_PID = 0;
    R8_USB_CTRL = RB_UC_HOST_MODE | RB_UC_INT_BUSY | RB_UC_DMA_EN;
    R8_UH_SETUP = RB_UH_SOF_EN;
    R8_USB_INT_FG = 0xff;
    R8_USB_INT_EN = RB_UIE_TRANSFER | RB_UIE_DETECT;
    return (R8_USB_CTRL & RB_UC_HOST_MODE) != 0;
}
bool hcd_deinit(uint8_t port)
{
    hcd_int_disable(port);
    stop();
    R8_USB_INT_EN = 0;
    R8_USB_CTRL = 0;
    memset(channels, 0, sizeof(channels));
    return true;
}
void hcd_int_enable(uint8_t port)
{
    (void)port;
    irq_enabled = 1;
    PFIC_EnableIRQ(USB_IRQn);
}
void hcd_int_disable(uint8_t port)
{
    (void)port;
    irq_enabled = 0;
    PFIC_DisableIRQ(USB_IRQn);
}
__INTERRUPT __HIGH_CODE void USB_IRQHandler(void)
{
    /* Main loop reads hardware completion before re-enabling this IRQ. */
    PFIC_DisableIRQ(USB_IRQn);
}
uint32_t hcd_frame_number(uint8_t port)
{
    (void)port;
    return tusb_time_millis_api();
}
bool hcd_port_connect_status(uint8_t port)
{
    (void)port;
    return (R8_USB_MIS_ST & RB_UMS_DEV_ATTACH) != 0;
}
tusb_speed_t hcd_port_speed_get(uint8_t port)
{
    (void)port;
    return (R8_USB_MIS_ST & RB_UMS_DM_LEVEL) ? TUSB_SPEED_LOW : TUSB_SPEED_FULL;
}
void hcd_port_reset(uint8_t port)
{
    (void)port;
    stop();
    R8_USB_DEV_AD = 0;
    R8_USB_CTRL &= ~RB_UC_LOW_SPEED;
    R8_UH_SETUP &= ~RB_UH_PRE_PID_EN;
    R8_UHOST_CTRL = RB_UH_BUS_RESET;
}
void hcd_port_reset_end(uint8_t port)
{
    R8_UHOST_CTRL =
        RB_UH_PORT_EN | (hcd_port_speed_get(port) == TUSB_SPEED_LOW ? RB_UH_LOW_SPEED : 0);
    R8_USB_INT_FG = RB_UIF_DETECT;
}
bool hcd_edpt_open(uint8_t port, uint8_t addr, const tusb_desc_endpoint_t *ep)
{
    (void)port;
    uint16_t mps = tu_edpt_packet_size(ep);
    if (!mps || mps > 64 || (tuh_speed_get(addr) == TUSB_SPEED_LOW && mps > 8) ||
        ep->bmAttributes.xfer == TUSB_XFER_ISOCHRONOUS)
        return false;
    channel_t *c = find(addr, ep->bEndpointAddress);
    if (!c)
        for (unsigned i = 0; i < EP_COUNT; i++)
            if (!channels[i].used)
            {
                c = &channels[i];
                break;
            }
    if (!c)
        return false;
    memset(c, 0, sizeof(*c));
    c->used = 1;
    c->addr = addr;
    c->ep = ep->bEndpointAddress;
    c->mps = (uint8_t)mps;
    c->type = ep->bmAttributes.xfer;
    c->interval = ep->bInterval ? ep->bInterval : 1;
    return true;
}
bool hcd_edpt_abort_xfer(uint8_t port, uint8_t addr, uint8_t ep)
{
    (void)port;
    channel_t *c = find(addr, ep);
    if (!c)
        return false;
    if (active >= 0 && c == &channels[active])
    {
        stop();
        draining = true;
        drain_started = tusb_time_millis_api();
    }
    c->pending = 0;
    c->epoch = ++next_epoch;
    return true;
}
bool hcd_edpt_close(uint8_t port, uint8_t addr, uint8_t ep)
{
    channel_t *c = find(addr, ep);
    if (!c)
        return false;
    hcd_edpt_abort_xfer(port, addr, ep);
    c->used = 0;
    return true;
}
void hcd_device_close(uint8_t port, uint8_t addr)
{
    for (unsigned i = 0; i < EP_COUNT; i++)
        if (channels[i].used && channels[i].addr == addr)
            hcd_edpt_close(port, addr, channels[i].ep);
}
bool hcd_edpt_clear_stall(uint8_t port, uint8_t addr, uint8_t ep)
{
    (void)port;
    channel_t *c = find(addr, ep);
    if (!c)
        return false;
    c->toggle = 0;
    return true;
}
bool hcd_edpt_xfer(uint8_t port, uint8_t addr, uint8_t ep, uint8_t *buffer, uint16_t length)
{
    (void)port;
    channel_t *c = find(addr, ep);
    if (!c || c->pending || (length && !buffer))
        return false;
    c->epoch = ++next_epoch;
    c->buffer = buffer;
    c->length = length;
    c->done = 0;
    c->pid = (ep & 0x80) ? USB_PID_IN : USB_PID_OUT;
    if (!(ep & 15u))
        c->toggle = 1; /* each control data/status starts DATA1 */
    c->started = tusb_time_millis_api();
    c->due = c->started;
    c->pending = 1;
    return true;
}
bool hcd_setup_send(uint8_t port, uint8_t addr, const uint8_t setup[8])
{
    if (!hcd_edpt_xfer(port, addr, 0, (uint8_t *)setup, 8))
        return false;
    channel_t *c = find(addr, 0);
    c->pid = USB_PID_SETUP;
    c->toggle = 0;
    return true;
}
void hcd_int_handler(uint8_t port, bool in_isr)
{
    (void)in_isr;
    uint32_t now = tusb_time_millis_api();
    bool present = hcd_port_connect_status(port);
    if (R8_USB_INT_FG & RB_UIF_DETECT)
        R8_USB_INT_FG = RB_UIF_DETECT;
    if (present != attached)
    {
        attached = present;
        if (present)
            hcd_event_device_attach(port, false);
        else
        {
            stop();
            memset(channels, 0, sizeof(channels));
            hcd_event_device_remove(port, false);
        }
    }
    if (active >= 0 && (R8_USB_INT_FG & RB_UIF_TRANSFER))
    {
        channel_t *c = &channels[active];
        uint8_t status = R8_USB_INT_ST, result = status & 15u;
        uint8_t n = (c->pid == USB_PID_IN) ? R8_USB_RX_LEN : R8_UH_TX_LEN;
        stop();
        if (status & RB_UIS_TOG_OK)
        {
            if (n > c->length - c->done || n > c->mps)
                finish(c, XFER_RESULT_FAILED);
            else
            {
                if (n && c->pid == USB_PID_IN)
                    memcpy(c->buffer + c->done, rx_dma, n);
                c->done += n;
                c->toggle ^= 1;
                if (c->done == c->length || (c->pid == USB_PID_IN && n < c->mps))
                    finish(c, XFER_RESULT_SUCCESS);
                else
                    c->due = now;
            }
        }
        else if (result == USB_PID_STALL)
            finish(c, XFER_RESULT_STALLED);
        else if (result == USB_PID_NAK || result == USB_PID_DATA0 || result == USB_PID_DATA1)
        {
            /* NAK/duplicate packet: yield the bus, retain toggle and deadline. */
            c->due = now + (c->type == TUSB_XFER_INTERRUPT ? c->interval : 1);
        }
        else
            finish(c, XFER_RESULT_FAILED);
    }
    for (unsigned i = 0; i < EP_COUNT; i++)
    {
        channel_t *c = &channels[i];
        /* Idle interrupt-IN polling may NAK indefinitely, but never owns the bus.
         * Application cancellation/deadline bounds requested auth transactions. */
        if (c->pending &&
            (((active == (int)i) && (uint32_t)(now - packet_started) >= 10) ||
             ((uint32_t)(now - c->started) >= 500 && c->type != TUSB_XFER_INTERRUPT)))
        {
            if (active == (int)i)
            {
                stop();
                draining = true;
                drain_started = now;
            }
            finish(c, XFER_RESULT_TIMEOUT);
        }
    }
    if (draining)
    {
        /* An aborted token may finish after EP_PID was cleared. Do not reuse
         * DMA or accept its late completion until the SIE is actually idle. */
        R8_USB_INT_FG = RB_UIF_TRANSFER;
        if (R8_USB_MIS_ST & RB_UMS_SIE_FREE)
            draining = false;
        else if ((uint32_t)(now - drain_started) >= 10)
        {
            uint8_t base = R8_USB_CTRL;
            R8_USB_CTRL = base | RB_UC_RESET_SIE | RB_UC_CLR_ALL;
            R8_USB_CTRL = base;
            R8_USB_INT_FG = RB_UIF_TRANSFER;
            draining = false;
        }
        else
            return;
    }
    if (active >= 0 || !attached || (R8_UHOST_CTRL & RB_UH_BUS_RESET) ||
        !(R8_USB_MIS_ST & RB_UMS_SIE_FREE))
        return;
    for (unsigned count = 0; count < EP_COUNT; count++)
    {
        cursor = (cursor + 1) % EP_COUNT;
        channel_t *c = &channels[cursor];
        if (!c->pending || (int32_t)(now - c->due) < 0)
            continue;
        bool low = tuh_speed_get(c->addr) == TUSB_SPEED_LOW;
        bool rootlow = hcd_port_speed_get(port) == TUSB_SPEED_LOW;
        R8_USB_DEV_AD = c->addr;
        if (low)
            R8_USB_CTRL |= RB_UC_LOW_SPEED;
        else
            R8_USB_CTRL &= ~RB_UC_LOW_SPEED;
        if (rootlow)
            R8_UHOST_CTRL |= RB_UH_LOW_SPEED;
        else
            R8_UHOST_CTRL &= ~RB_UH_LOW_SPEED;
        if (low && !rootlow)
            R8_UH_SETUP |= RB_UH_PRE_PID_EN;
        else
            R8_UH_SETUP &= ~RB_UH_PRE_PID_EN;
        uint16_t n = TU_MIN(c->mps, c->length - c->done);
        if (c->pid != USB_PID_IN && n)
            memcpy(tx_dma, c->buffer + c->done, n);
        R8_UH_TX_LEN = (uint8_t)n;
        R8_UH_TX_CTRL = c->toggle ? RB_UH_T_TOG : 0;
        R8_UH_RX_CTRL = c->toggle ? RB_UH_R_TOG : 0;
        R8_USB_INT_FG = RB_UIF_TRANSFER;
        active = cursor;
        packet_started = now;
        R8_UH_EP_PID = (c->pid << 4) | (c->ep & 15u);
        break;
    }
}
void hcd_ch585_poll(void)
{
    PFIC_DisableIRQ(USB_IRQn);
    hcd_int_handler(0, false);
    if (irq_enabled)
        PFIC_EnableIRQ(USB_IRQn);
}

bool hcd_xfer_event_valid(uint8_t addr, uint8_t ep, uint32_t epoch)
{
    channel_t *c = find(addr, ep);
    return c && c->epoch == epoch;
}
