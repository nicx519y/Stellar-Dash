#include "usb_diagnostics_tx.h"
#include "CH58x_common.h"
#include "usb_host.h"
#include <string.h>
typedef struct
{
    uint32_t at, count;
    uint8_t current, last, detail;
} fault_record_t;
static volatile fault_record_t faults[2];
void usb_diag_fault_record(uint8_t source, uint8_t code, uint8_t detail)
{
    if (source >= 2)
        return;
    faults[source].at = usb_host_clock_snapshot();
    faults[source].count++;
    faults[source].current = code;
    faults[source].last = code;
    faults[source].detail = detail;
}
void usb_diag_fault_ack(uint8_t source)
{
    if (source < 2)
        faults[source].current = 0;
}
bool usb_diagnostics_query(uint8_t page, uint8_t index, uint8_t out[56], uint8_t *length)
{
    if (page != XORA_USB_DIAG_INTERFACE && page != XORA_USB_DIAG_HOST_DEVICE && index != 0)
        return false;
    memset(out, 0, 56);
    uint16_t n = 0;
    uint8_t *p = out + 12;
    switch (page)
    {
    case XORA_USB_DIAG_HOST:
    case XORA_USB_DIAG_INTERFACE:
    case XORA_USB_DIAG_HOST_DEVICE:
        n = usb_host_diag(page, index, p, 44);
        break;
    case XORA_USB_DIAG_AUTH:
        n = usb_auth_host_diag(p);
        break;
    case XORA_USB_DIAG_DEVICE:
        n = usb_device_hw_diag(p);
        break;
    case XORA_USB_DIAG_FAULTS:
    {
        uint32_t spi = PFIC_GetStatusIRQ(SPI0_IRQn), gpio = PFIC_GetStatusIRQ(GPIO_A_IRQn);
        PFIC_DisableIRQ(SPI0_IRQn);
        PFIC_DisableIRQ(GPIO_A_IRQn);
        for (unsigned i = 0; i < 2; i++)
        {
            p[0] = i;
            p[1] = faults[i].current;
            p[2] = faults[i].last;
            p[3] = faults[i].detail;
            usb_diag_put32(p + 4, faults[i].at);
            usb_diag_put32(p + 8, faults[i].count);
            p += 12;
        }
        if (gpio)
            PFIC_EnableIRQ(GPIO_A_IRQn);
        if (spi)
            PFIC_EnableIRQ(SPI0_IRQn);
        n = 24;
        break;
    }
    default:
        return false;
    }
    if (!n || n > 44)
        return false;
    out[0] = XORA_USB_DIAG_VERSION;
    out[1] = page;
    out[2] = index;
    out[3] = (uint8_t)n;
    usb_diag_put32(out + 4, usb_host_clock_snapshot());
    usb_diag_put32(out + 8, usb_host_generation());
    *length = (uint8_t)(12 + n);
    return true;
}
