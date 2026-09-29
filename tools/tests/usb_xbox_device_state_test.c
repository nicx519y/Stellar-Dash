#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "usb_auth.h"
#include "usb_gip_protocol.h"
#include "usb_xbox_device.h"

static bool s_authenticated;
static uint8_t s_forwarded_auth[USB_GIP_PACKET_MAX_BYTES];
static uint8_t s_forwarded_auth_length;

bool usb_auth_is_authenticated(usb_auth_scheme_t scheme)
{
    return (scheme == USB_AUTH_SCHEME_XBOX_GIP) && s_authenticated;
}

bool usb_auth_gip_device_out(const uint8_t *report, uint8_t length)
{
    assert(report != NULL);
    assert(length <= sizeof(s_forwarded_auth));
    memcpy(s_forwarded_auth, report, length);
    s_forwarded_auth_length = length;
    return true;
}

bool usb_auth_gip_device_in_pending(void)
{
    return false;
}

bool usb_auth_gip_device_in(uint8_t *report,
                            uint8_t capacity,
                            uint8_t *length)
{
    (void)report;
    (void)capacity;
    if(length != NULL)
    {
        *length = 0u;
    }
    return false;
}

static void send_ack(uint8_t sequence)
{
    const uint8_t ack[13] = {
        0x01u, USB_GIP_FLAG_INTERNAL, sequence, 0x09u,
        0x00u, 0x04u, USB_GIP_FLAG_INTERNAL, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    assert(usb_xbox_device_out(ack, sizeof(ack)));
}

static void test_announce_and_idle(void)
{
    uint8_t packet[USB_XBOX_DEVICE_PACKET_BYTES];
    uint8_t length = 0u;

    usb_xbox_device_init();
    usb_xbox_device_set_mounted(true, 1000u);
    usb_xbox_device_process(1499u);
    assert(usb_xbox_device_next_in(packet, sizeof(packet), &length));
    assert(length == USB_XBOX_DEVICE_INPUT_BYTES);
    assert(packet[0] == 0x20u);
    assert(packet[2] == 0u);
    assert(packet[3] == 32u);
    assert(packet[10] == 0xFFu && packet[11] == 0xFFu);
    assert(packet[14] == 0xFFu && packet[15] == 0xFFu);

    usb_xbox_device_process(1500u);
    assert(usb_xbox_device_next_in(packet, sizeof(packet), &length));
    assert(packet[0] == 0x02u);
    assert(packet[1] == USB_GIP_FLAG_INTERNAL);
    assert(packet[2] == 1u);
    assert(length == 32u);
    assert(packet[3] == 28u);
    assert(packet[4] == 0x00u && packet[5] == 0x2Au &&
           packet[6] == 0x00u);
    assert(packet[12] == 0xDFu && packet[13] == 0x33u);
}

static void test_descriptor_chunk_flow(void)
{
    static const uint8_t descriptor_golden[202] = {
        0x10u,0x00u,0x01u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0xCAu,0x00u,
        0x8Bu,0x00u,0x16u,0x00u,0x1Fu,0x00u,0x20u,0x00u,
        0x27u,0x00u,0x2Du,0x00u,0x4Au,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x02u,0x01u,
        0x00u,0x00u,0x00u,0x01u,0x00u,0x01u,0x00u,0x00u,
        0x06u,0x01u,0x02u,0x03u,0x04u,0x06u,0x07u,0x05u,
        0x01u,0x04u,0x05u,0x06u,0x0Au,0x01u,0x1Au,0x00u,
        0x57u,0x69u,0x6Eu,0x64u,0x6Fu,0x77u,0x73u,0x2Eu,
        0x58u,0x62u,0x6Fu,0x78u,0x2Eu,0x49u,0x6Eu,0x70u,
        0x75u,0x74u,0x2Eu,0x47u,0x61u,0x6Du,0x65u,0x70u,
        0x61u,0x64u,0x04u,0x56u,0xFFu,0x76u,0x97u,0xFDu,
        0x9Bu,0x81u,0x45u,0xADu,0x45u,0xB6u,0x45u,0xBBu,
        0xA5u,0x26u,0xD6u,0x2Cu,0x40u,0x2Eu,0x08u,0xDFu,
        0x07u,0xE1u,0x45u,0xA5u,0xABu,0xA3u,0x12u,0x7Au,
        0xF1u,0x97u,0xB5u,0xE7u,0x1Fu,0xF3u,0xB8u,0x86u,
        0x73u,0xE9u,0x40u,0xA9u,0xF8u,0x2Fu,0x21u,0x26u,
        0x3Au,0xCFu,0xB7u,0xFEu,0xD2u,0xDDu,0xECu,0x87u,
        0xD3u,0x94u,0x42u,0xBDu,0x96u,0x1Au,0x71u,0x2Eu,
        0x3Du,0xC7u,0x7Du,0x02u,0x17u,0x00u,0x20u,0x20u,
        0x00u,0x01u,0x00u,0x10u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x17u,0x00u,0x09u,0x3Cu,0x00u,
        0x01u,0x00u,0x08u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u
    };
    const uint8_t request[] = {
        0x04u, USB_GIP_FLAG_INTERNAL, 7u, 0u
    };
    uint8_t packet[USB_XBOX_DEVICE_PACKET_BYTES];
    uint8_t length = 0u;
    uint8_t packets = 0u;
    usb_gip_rx_t descriptor_rx;

    usb_gip_rx_reset(&descriptor_rx);
    assert(usb_xbox_device_out(request, sizeof(request)));
    while(descriptor_rx.complete == 0u)
    {
        assert(usb_xbox_device_next_in(packet, sizeof(packet), &length));
        assert(packet[0] == 0x04u);
        assert(packet[2] == 7u);
        assert(usb_gip_rx_consume(&descriptor_rx, packet, length));
        ++packets;
        assert(packets <= 8u);
        if((packet[1] & USB_GIP_FLAG_NEEDS_ACK) != 0u)
        {
            send_ack(packet[2]);
        }
    }
    assert(packets == 5u);
    assert(descriptor_rx.data_length == sizeof(descriptor_golden));
    assert(memcmp(descriptor_rx.data,
                  descriptor_golden,
                  sizeof(descriptor_golden)) == 0);
}

static void test_auth_forwarding_and_authenticated_reports(void)
{
    const uint8_t auth[] = {
        0x06u, USB_GIP_FLAG_INTERNAL, 8u, 2u, 0x01u, 0x00u
    };
    uint8_t packet[USB_XBOX_DEVICE_PACKET_BYTES];
    uint8_t length = 0u;

    memset(s_forwarded_auth, 0, sizeof(s_forwarded_auth));
    s_forwarded_auth_length = 0u;
    assert(usb_xbox_device_out(auth, sizeof(auth)));
    assert(s_forwarded_auth_length == sizeof(auth));
    assert(memcmp(s_forwarded_auth, auth, sizeof(auth)) == 0);

    s_authenticated = true;
    usb_xbox_device_process(1600u);
    assert(usb_xbox_device_next_in(packet, sizeof(packet), &length));
    assert(packet[0] == 0x20u && length == 36u);

    usb_xbox_device_set_actions(
        (1ul << 16) | (1ul << 4) | (1ul << 0) | (1ul << 10));
    usb_xbox_device_process(1601u);
    assert(usb_xbox_device_next_in(packet, sizeof(packet), &length));
    assert(packet[0] == 0x07u);
    assert(packet[1] == USB_GIP_FLAG_INTERNAL);
    assert(length == 6u);
    assert(packet[4] == 1u && packet[5] == 0x5Bu);

    assert(usb_xbox_device_next_in(packet, sizeof(packet), &length));
    assert(packet[0] == 0x20u && length == 36u);
    assert((packet[4] & (1u << 4)) != 0u);
    assert((packet[5] & (1u << 0)) != 0u);
    assert(packet[6] == 0xFFu && packet[7] == 0x03u);

    usb_xbox_device_process(16601u);
    assert(usb_xbox_device_next_in(packet, sizeof(packet), &length));
    assert(packet[0] == 0x03u);
    assert(packet[1] == USB_GIP_FLAG_INTERNAL);
    assert(length == 8u);
    assert(packet[4] == 0x80u);
}

static void test_out_report_ack(void)
{
    const uint8_t led[] = {
        0x0Au, USB_GIP_FLAG_NEEDS_ACK, 9u, 3u,
        0x00u, 0x02u, 0x14u
    };
    uint8_t packet[USB_XBOX_DEVICE_PACKET_BYTES];
    uint8_t length = 0u;

    assert(usb_xbox_device_out(led, sizeof(led)));
    assert(usb_xbox_device_next_in(packet, sizeof(packet), &length));
    assert(length == 13u);
    assert(packet[0] == 0x01u);
    assert(packet[1] == USB_GIP_FLAG_INTERNAL);
    assert(packet[2] == 9u);
    assert(packet[5] == 0x0Au);
}

int main(void)
{
    test_announce_and_idle();
    test_descriptor_chunk_flow();
    test_auth_forwarding_and_authenticated_reports();
    test_out_report_ack();
    return 0;
}
