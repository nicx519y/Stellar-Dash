#include "usb_hid_if.h"

#include <stdbool.h>
#include <string.h>

#include "dongle_config.h"
#include "platform_port.h"
#ifdef CH585
#include "CH58x_common.h"
#if DONGLE_USE_USBHS_BACKEND
#include "ch585_usbhs_device.h"
#endif
#endif

static bool s_usb_configured;
static xinput_report_t s_last_report;
static uint32_t s_report_sent_count;
static uint32_t s_telemetry_drop_count;

#define TELEMETRY_QUEUE_DEPTH 8u
#define TELEMETRY_MAX_BYTES   32u

typedef struct {
    uint8_t data[TELEMETRY_MAX_BYTES];
    uint16_t len;
} telemetry_slot_t;

static telemetry_slot_t s_tlm_queue[TELEMETRY_QUEUE_DEPTH];
static uint8_t s_tlm_head;
static uint8_t s_tlm_tail;
static uint8_t s_tlm_count;
#ifdef CH585
static uint8_t s_dev_config;
static uint8_t s_setup_req;
static uint16_t s_setup_len;
static const uint8_t *s_desc_ptr;

#if !DONGLE_USE_USBHS_BACKEND
__attribute__((aligned(4))) static uint8_t s_ep0_buf[64 + 64 + 64];
__attribute__((aligned(4))) static uint8_t s_ep1_buf[64 + 64];
__attribute__((aligned(4))) static uint8_t s_ep2_buf[64 + 64];
__attribute__((aligned(4))) static uint8_t s_ep3_buf[64 + 64];
#endif

/* Xbox360 wired-compatible VID/PID with vendor-specific interface class. */
static const uint8_t s_dev_desc[] = {
    0x12u, 0x01u, 0x10u, 0x01u, 0xFFu, 0x5Du, 0x01u, 0x40u,
    0x5Eu, 0x04u, 0x8Eu, 0x02u, 0x14u, 0x01u, 0x01u, 0x02u,
    0x00u, 0x01u
};

static const uint8_t s_cfg_desc[] = {
    0x09u, 0x02u, 0x20u, 0x00u, 0x01u, 0x01u, 0x00u, 0x80u, 0x32u,
    0x09u, 0x04u, 0x00u, 0x00u, 0x02u, 0xFFu, 0x5Du, 0x01u, 0x00u,
    0x07u, 0x05u, 0x81u, 0x03u, 0x20u, 0x00u, 0x04u,
    0x07u, 0x05u, 0x02u, 0x03u, 0x20u, 0x00u, 0x04u
};

static const uint8_t s_lang_desc[] = {0x04u, 0x03u, 0x09u, 0x04u};
static const uint8_t s_manu_desc[] = {0x12u, 0x03u, 'H', 0, 'B', 0, 'o', 0, 'x', 0, ' ', 0, 'R', 0, 'F', 0,};
static const uint8_t s_prod_desc[] = {0x24u, 0x03u, 'H',0,'B',0,'o',0,'x',0,' ',0,'X',0,'I',0,'n',0,'p',0,'u',0,'t',0,' ',0,'D',0,'o',0,'n',0,'g',0,'l',0,'e',0};

static void usb_write_ep0_data(const uint8_t *src, uint8_t len)
{
    if ((src != 0) && (len > 0u)) {
        memcpy(pEP0_DataBuf, src, len);
    }
    R8_UEP0_T_LEN = len;
}

static void usb_handle_setup(void)
{
    uint8_t len = 0u;
    uint8_t err = 0u;
    uint8_t req_type;

    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;

    s_setup_len = pSetupReqPak->wLength;
    s_setup_req = pSetupReqPak->bRequest;
    req_type = pSetupReqPak->bRequestType;

    if ((req_type & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
        err = 0xFFu;
    } else {
        switch (s_setup_req) {
        case USB_GET_DESCRIPTOR:
            switch ((pSetupReqPak->wValue >> 8) & 0xFFu) {
            case USB_DESCR_TYP_DEVICE:
                s_desc_ptr = s_dev_desc;
                len = s_dev_desc[0];
                break;
            case USB_DESCR_TYP_CONFIG:
                s_desc_ptr = s_cfg_desc;
                len = s_cfg_desc[2];
                break;
            case USB_DESCR_TYP_STRING:
                switch (pSetupReqPak->wValue & 0xFFu) {
                case 0u: s_desc_ptr = s_lang_desc; len = s_lang_desc[0]; break;
                case 1u: s_desc_ptr = s_manu_desc; len = s_manu_desc[0]; break;
                case 2u: s_desc_ptr = s_prod_desc; len = s_prod_desc[0]; break;
                default: err = 0xFFu; break;
                }
                break;
            default:
                err = 0xFFu;
                break;
            }
            if (err == 0u) {
                if (s_setup_len < len) len = (uint8_t)s_setup_len;
                if (len > 64u) len = 64u;
                usb_write_ep0_data(s_desc_ptr, len);
                s_desc_ptr += len;
                s_setup_len -= len;
            }
            break;

        case USB_SET_ADDRESS:
            s_setup_len = (uint16_t)(pSetupReqPak->wValue & 0x7Fu);
            usb_write_ep0_data(0, 0u);
            break;

        case USB_GET_CONFIGURATION:
            pEP0_DataBuf[0] = s_dev_config;
            usb_write_ep0_data(pEP0_DataBuf, 1u);
            break;

        case USB_SET_CONFIGURATION:
            s_dev_config = (uint8_t)(pSetupReqPak->wValue & 0xFFu);
            s_usb_configured = (s_dev_config != 0u);
            usb_write_ep0_data(0, 0u);
            break;

        case USB_GET_STATUS:
            pEP0_DataBuf[0] = 0u;
            pEP0_DataBuf[1] = 0u;
            usb_write_ep0_data(pEP0_DataBuf, 2u);
            break;

        default:
            err = 0xFFu;
            break;
        }
    }

    if (err != 0u) {
        R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
    } else {
        R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
    }
}

void USB_DevTransProcess(void)
{
    uint8_t intflag = R8_USB_INT_FG;
    uint8_t len;

    if (intflag & RB_UIF_TRANSFER) {
        if (R8_USB_INT_ST & RB_UIS_SETUP_ACT) {
            usb_handle_setup();
            R8_USB_INT_FG = RB_UIF_TRANSFER;
            return;
        }

        switch (R8_USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP)) {
        case UIS_TOKEN_IN:
            if (s_setup_req == USB_GET_DESCRIPTOR) {
                len = (s_setup_len >= 64u) ? 64u : (uint8_t)s_setup_len;
                usb_write_ep0_data(s_desc_ptr, len);
                s_desc_ptr += len;
                s_setup_len -= len;
                R8_UEP0_CTRL ^= RB_UEP_T_TOG;
            } else if (s_setup_req == USB_SET_ADDRESS) {
                R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | (uint8_t)(s_setup_len & 0x7Fu);
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
            } else {
                R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
            }
            break;

        case UIS_TOKEN_IN | 1:
            R8_UEP1_CTRL ^= RB_UEP_T_TOG;
            R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
            break;

        case UIS_TOKEN_OUT | 1:
            if (R8_USB_INT_ST & RB_UIS_TOG_OK) {
                R8_UEP1_CTRL ^= RB_UEP_R_TOG;
            }
            break;
        case UIS_TOKEN_OUT | 2:
            if (R8_USB_INT_ST & RB_UIS_TOG_OK) {
                R8_UEP2_CTRL ^= RB_UEP_R_TOG;
            }
            break;

        default:
            break;
        }
        R8_USB_INT_FG = RB_UIF_TRANSFER;
    } else if (intflag & RB_UIF_BUS_RST) {
        R8_USB_DEV_AD = 0u;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
        s_dev_config = 0u;
        s_usb_configured = false;
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    } else if (intflag & RB_UIF_SUSPEND) {
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    } else {
        R8_USB_INT_FG = intflag;
    }
}

__INTERRUPT
void USB_IRQHandler(void)
{
    USB_DevTransProcess();
}
#endif

__attribute__((weak))
bool usb_hw_ready(void)
{
#ifdef CH585
#if DONGLE_USE_USBHS_BACKEND
    return (USBHS_DevEnumStatus != 0u);
#else
    return s_usb_configured;
#endif
#else
    return true;
#endif
}

__attribute__((weak))
bool usb_hw_can_send_xinput(void)
{
#ifdef CH585
#if DONGLE_USE_USBHS_BACKEND
    return ((USBHS_DevEnumStatus != 0u) && ((USBHS_Endp_Busy[DEF_UEP2] & DEF_UEP_BUSY) == 0u));
#else
    return (s_usb_configured && ((EP1_GetINSta()) != 0u));
#endif
#else
    return true;
#endif
}

__attribute__((weak))
bool usb_hw_can_send_telemetry(void)
{
#ifdef CH585
#if DONGLE_USE_USBHS_BACKEND
    return ((USBHS_DevEnumStatus != 0u) && ((USBHS_Endp_Busy[DEF_UEP5] & DEF_UEP_BUSY) == 0u));
#else
    return s_usb_configured;
#endif
#else
    return true;
#endif
}

__attribute__((weak))
bool usb_hw_send_xinput_report(const xinput_report_t *report)
{
#ifdef CH585
#if DONGLE_USE_USBHS_BACKEND
    if ((report == 0) || (USBHS_DevEnumStatus == 0u)) {
        return false;
    }
    if (USBHS_Endp_DataUp(DEF_UEP2, (uint8_t *)report, XINPUT_ENDPOINT_SIZE, DEF_UEP_DMA_LOAD) != 0u) {
        return false;
    }
    return true;
#else
    if ((report == 0) || !s_usb_configured) {
        return false;
    }
    if ((EP1_GetINSta()) == 0u) {
        return false;
    }
    memcpy(pEP1_IN_DataBuf, report, XINPUT_ENDPOINT_SIZE);
    DevEP1_IN_Deal(XINPUT_ENDPOINT_SIZE);
    return true;
#endif
#else
    (void)report;
    return true;
#endif
}

__attribute__((weak))
bool usb_hw_send_telemetry_report(const uint8_t *payload, uint16_t len)
{
#ifdef CH585
#if DONGLE_USE_USBHS_BACKEND
    uint16_t tx_len;
    if ((payload == 0) || (len == 0u) || (USBHS_DevEnumStatus == 0u)) {
        return false;
    }
    tx_len = (len > 64u) ? 64u : len;
    if (USBHS_Endp_DataUp(DEF_UEP5, (uint8_t *)payload, tx_len, DEF_UEP_CPY_LOAD) != 0u) {
        return false;
    }
    return true;
#else
    (void)payload;
    (void)len;
    return true;
#endif
#else
    (void)payload;
    (void)len;
    return true;
#endif
}

/* Device descriptor stub for XInput-style enumeration path. */
static const uint8_t s_xinput_device_desc[] = {
    0x12u, 0x01u, 0x00u, 0x02u, 0xFFu, 0xFFu, 0xFFu, 0x40u,
    0x5Eu, 0x04u, 0x5Fu, 0x58u, 0x00u, 0x01u, 0x01u, 0x02u,
    0x03u, 0x01u
};

void usb_hid_init(void)
{
    s_usb_configured = true;
    memset(&s_last_report, 0, sizeof(s_last_report));
    s_last_report.report_size = XINPUT_ENDPOINT_SIZE;
    s_report_sent_count = 0u;
    s_telemetry_drop_count = 0u;
    s_tlm_head = 0u;
    s_tlm_tail = 0u;
    s_tlm_count = 0u;
    (void)s_xinput_device_desc;

#ifdef CH585
#if DONGLE_USE_USBHS_BACKEND
    s_usb_configured = true;
    /* Required by SDK USBHS compatibility HID stack globals. */
    extern volatile uint16_t Data_Pack_Max_Len;
    extern volatile uint16_t Head_Pack_Len;
    Data_Pack_Max_Len = XINPUT_ENDPOINT_SIZE;
    Head_Pack_Len = 0u;
    USBHS_Device_Init(ENABLE);
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);
#else
    s_dev_config = 0u;
    s_setup_req = 0u;
    s_setup_len = 0u;
    s_desc_ptr = 0;
    pEP0_RAM_Addr = s_ep0_buf;
    pEP1_RAM_Addr = s_ep1_buf;
    pEP2_RAM_Addr = s_ep2_buf;
    pEP3_RAM_Addr = s_ep3_buf;
    USB_DeviceInit();
    /* Force a short detach/attach so host can re-enumerate reliably. */
    R16_PIN_CONFIG &= (uint16_t)(~RB_UDP_PU_EN);
    {
        uint32_t t0 = platform_now_us();
        while ((uint32_t)(platform_now_us() - t0) < 10000u) {
            /* spin */
        }
    }
    R16_PIN_CONFIG |= RB_UDP_PU_EN;
    PFIC_EnableIRQ(USB_IRQn);
    s_usb_configured = false;
#endif
#endif
}

void usb_hid_poll(void)
{
    telemetry_slot_t *slot;
    uint8_t next_tail;

#ifdef CH585
#if !DONGLE_USE_USBHS_BACKEND
    /* Poll-transfer fallback in case USB IRQ is not firing as expected. */
    if (R8_USB_INT_FG != 0u) {
        USB_DevTransProcess();
    }
#endif
#endif

    if (!s_usb_configured || (s_tlm_count == 0u)) {
        return;
    }
    if (!usb_hw_ready() || !usb_hw_can_send_telemetry()) {
        return;
    }

    slot = &s_tlm_queue[s_tlm_tail];
    if (slot->len == 0u) {
        s_tlm_count--;
        next_tail = (uint8_t)(s_tlm_tail + 1u);
        if (next_tail >= TELEMETRY_QUEUE_DEPTH) next_tail = 0u;
        s_tlm_tail = next_tail;
        return;
    }

    if (!usb_hw_send_telemetry_report(slot->data, slot->len)) {
        return;
    }

    slot->len = 0u;
    s_tlm_count--;
    next_tail = (uint8_t)(s_tlm_tail + 1u);
    if (next_tail >= TELEMETRY_QUEUE_DEPTH) next_tail = 0u;
    s_tlm_tail = next_tail;
}

bool usb_hid_ready(void)
{
    return (s_usb_configured && usb_hw_ready());
}

bool usb_hid_can_send(void)
{
    return (s_usb_configured && usb_hw_ready() && usb_hw_can_send_xinput());
}

bool usb_hid_try_send_report(const xinput_report_t *report)
{
    if (report == 0) {
        return false;
    }
    if (!usb_hid_can_send()) {
        return false;
    }

    s_last_report = *report;

    if (!usb_hw_send_xinput_report(report)) {
        return false;
    }
    s_report_sent_count++;
    return true;
}

bool usb_hid_try_send_telemetry(const uint8_t *payload, uint16_t len)
{
    uint8_t next_head;
    telemetry_slot_t *slot;

    if ((payload == 0) || (len == 0u) || (len > TELEMETRY_MAX_BYTES)) {
        return false;
    }
    if (!s_usb_configured) {
        return false;
    }
    if (s_tlm_count >= TELEMETRY_QUEUE_DEPTH) {
        s_telemetry_drop_count++;
        return false;
    }

    slot = &s_tlm_queue[s_tlm_head];
    memcpy(slot->data, payload, len);
    slot->len = len;
    s_tlm_count++;

    next_head = (uint8_t)(s_tlm_head + 1u);
    if (next_head >= TELEMETRY_QUEUE_DEPTH) next_head = 0u;
    s_tlm_head = next_head;

    /* Opportunistically flush one telemetry frame without blocking. */
    usb_hid_poll();
    return true;
}

uint32_t usb_hid_report_sent_count(void)
{
    return s_report_sent_count;
}

uint32_t usb_hid_telemetry_drop_count(void)
{
    return s_telemetry_drop_count;
}
