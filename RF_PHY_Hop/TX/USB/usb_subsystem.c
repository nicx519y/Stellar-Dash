#include "usb_board_link.h"

#include "CH58x_common.h"
#include "board_latest_ch585.h"
#include "usb_auth.h"
#include "usb_device.h"
#include "usb_host.h"
#include "usb_management_control.h"
#include "usb_net_bridge.h"

static bool usb_subsystem_stage_auth_blob(const uint8_t *data,
                                          uint16_t length)
{
    uint16_t offset = 0u;

    if((length > USB_AUTH_BLOB_MAX_BYTES) ||
       ((length != 0u) && (data == 0)))
    {
        return false;
    }
    if(length == 0u)
    {
        return usb_auth_write_blob(0u, 0u, 0, 0u, true);
    }

    while(offset < length)
    {
        const uint16_t remaining = (uint16_t)(length - offset);
        const uint8_t chunk =
            (remaining > UINT8_MAX) ? UINT8_MAX : (uint8_t)remaining;
        const bool final_fragment =
            ((uint16_t)(offset + chunk) == length);

        if(!usb_auth_write_blob(0u,
                                offset,
                                &data[offset],
                                chunk,
                                final_fragment))
        {
            usb_auth_clear();
            return false;
        }
        offset = (uint16_t)(offset + chunk);
    }
    return true;
}

static bool usb_subsystem_sink(usb_board_channel_t channel,
                               const uint8_t *data,
                               uint16_t length)
{
    switch(channel)
    {
    case USB_BOARD_CHANNEL_AUTH:
        return usb_subsystem_stage_auth_blob(data, length);
    case USB_BOARD_CHANNEL_USB_DEVICE:
        if(length > UINT8_MAX)
        {
            return false;
        }
        return usb_device_control(data, (uint8_t)length);
    case USB_BOARD_CHANNEL_USB_HOST:
        /*
         * Host EP0/interrupt traffic is intentionally never tunneled over
         * BoardLink.  Enumeration and authentication are fully handled by
         * usb_host/usb_auth_host on CH585 so their timing cannot depend on
         * STM32 SPI service latency.  USB_CONTROL is the separate, implemented
         * board-management RPC; raw host requests are outside that ABI.
         */
        return false;
    case USB_BOARD_CHANNEL_TELEMETRY:
        return usb_device_submit_telemetry(data, length);
    case USB_BOARD_CHANNEL_NETWORK:
        /* CDC-NCM was removed from the V2 WebConfig profile. */
        return false;
    case USB_BOARD_CHANNEL_WEBCONFIG:
        return usb_device_submit_webhid_report(data, length);
    default:
        return false;
    }
}

int usb_subsystem_run(usb_board_role_t role)
{
    bool host_initialized = false;

    if((role != USB_BOARD_ROLE_USB) &&
       (role != USB_BOARD_ROLE_MAINTENANCE))
    {
        return -1;
    }

    usb_auth_init();
    usb_management_control_init();
    usb_net_bridge_init();
    usb_net_bridge_set_sink(usb_subsystem_sink);
    usb_board_link_init(role);
    if(!usb_board_link_is_ready())
    {
        return -2;
    }
    /*
     * ROLE_SELECTED only acknowledges the cold-boot selector.  Publish the
     * Application-ready edge at the actual control-loop boundary, after RX DMA
     * and its interrupt routes are armed.  Optional USB Host initialization is
     * deliberately deferred until GET_CAPS has been parsed and its response
     * queued; maintenance control must not depend on a second peripheral.
     */
    rfm_board_latest_ch585_pulse_boot_ready();

    for(;;)
    {
        usb_board_link_process();
        if(!host_initialized && usb_board_link_caps_requested())
        {
            host_initialized = true;
            (void)usb_host_init();
        }
        usb_device_process();
        if(host_initialized)
        {
            usb_host_process();
        }
    }
}
