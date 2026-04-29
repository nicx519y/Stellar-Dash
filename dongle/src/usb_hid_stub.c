#include "usb_hid_if.h"

#include <stdbool.h>
#include <string.h>

static bool s_usb_configured;
static xinput_report_t s_last_report;

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
    (void)s_xinput_device_desc;

    /*
     * TODO: Replace with CH585 USB device init + XInput descriptor setup.
     * - Handle EP0 standard requests and return XInput descriptor sets.
     * - Configure IN endpoint 0x81 and OUT endpoint 0x02 interrupt transfer.
     * - Keep additional legacy XInput endpoints NAK-able for compatibility.
     */
}

bool usb_hid_ready(void)
{
    return s_usb_configured;
}

bool usb_hid_can_send(void)
{
    /* TODO: Return real endpoint-ready state from USB ISR/driver. */
    return true;
}

bool usb_hid_try_send_report(const xinput_report_t *report)
{
    if (report == 0) {
        return false;
    }

    s_last_report = *report;

    /*
     * TODO: Serialize XInput report and push to endpoint 0x81.
     * OUT endpoint packets (rumble/LED) should be forwarded over RF control channel.
     */
    return true;
}
