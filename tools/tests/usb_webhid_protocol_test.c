#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "usb_webhid.h"
#include "webhid_protocol.h"

static void test_protocol_layout(void)
{
    webhid_secure_report_v1_t report;
    uint8_t wire[WEBHID_REPORT_BYTES];
    uint8_t index;

    assert(sizeof(report) == WEBHID_REPORT_BYTES);
    assert(offsetof(webhid_secure_report_v1_t, sequence_le) == 4u);
    assert(offsetof(webhid_secure_report_v1_t, payload) == 8u);
    assert(offsetof(webhid_secure_report_v1_t, tag) == 52u);
    assert(sizeof(webhid_perf_sample_v1_t) ==
           WEBHID_PERF_SAMPLE_BYTES);
    assert(offsetof(webhid_perf_sample_v1_t, current_distance_um_le) ==
           8u);

    for(index = 0u; index < sizeof(wire); ++index)
    {
        wire[index] = (uint8_t)(index ^ 0xA5u);
    }
    memcpy(&report, wire, sizeof(report));
    assert(memcmp(&report, wire, sizeof(report)) == 0);
}

static void test_descriptors(void)
{
    const uint8_t *descriptor;
    uint16_t length;
    uint16_t offset;
    uint8_t interfaces = 0u;
    uint8_t endpoints = 0u;
    uint8_t saw_in = 0u;
    uint8_t saw_out = 0u;

    descriptor = usb_webhid_device_descriptor(&length);
    assert(descriptor != NULL);
    assert(length == USB_WEBHID_DEVICE_DESCRIPTOR_BYTES);
    assert(descriptor[8] == 0xFEu && descriptor[9] == 0xCAu);
    assert(descriptor[10] == 0x21u && descriptor[11] == 0x40u);

    descriptor = usb_webhid_qualifier_descriptor(&length);
    assert(descriptor != NULL);
    assert(length == USB_WEBHID_QUALIFIER_DESCRIPTOR_BYTES);
    assert(descriptor[1] == 0x06u);
    assert(descriptor[7] == USB_WEBHID_EP0_BYTES);

    descriptor = usb_webhid_configuration_descriptor(&length);
    assert(descriptor != NULL);
    assert(length == USB_WEBHID_CONFIG_DESCRIPTOR_BYTES);
    assert(descriptor[2] == USB_WEBHID_CONFIG_DESCRIPTOR_BYTES);
    assert(descriptor[3] == 0u);
    assert(descriptor[4] == 1u);

    offset = 0u;
    while((uint16_t)(offset + 2u) <= length)
    {
        const uint8_t item_length = descriptor[offset];
        const uint8_t item_type = descriptor[offset + 1u];
        assert(item_length >= 2u);
        assert((uint16_t)(offset + item_length) <= length);
        if(item_type == 0x04u)
        {
            ++interfaces;
            assert(descriptor[offset + 5u] == 0x03u);
            assert(descriptor[offset + 6u] == 0u);
            assert(descriptor[offset + 7u] == 0u);
        }
        else if(item_type == 0x05u)
        {
            const uint8_t address = descriptor[offset + 2u];
            ++endpoints;
            assert(descriptor[offset + 3u] == 0x03u);
            assert(descriptor[offset + 4u] == WEBHID_REPORT_BYTES);
            assert(descriptor[offset + 5u] == 0u);
            assert(descriptor[offset + 6u] == 1u);
            saw_in |= (address == USB_WEBHID_ENDPOINT_IN) ? 1u : 0u;
            saw_out |= (address == USB_WEBHID_ENDPOINT_OUT) ? 1u : 0u;
        }
        offset = (uint16_t)(offset + item_length);
    }
    assert(offset == length);
    assert(interfaces == 1u);
    assert(endpoints == 2u);
    assert(saw_in != 0u && saw_out != 0u);

    descriptor = usb_webhid_other_speed_descriptor(&length);
    assert(descriptor != NULL);
    assert(length == USB_WEBHID_CONFIG_DESCRIPTOR_BYTES);
    assert(descriptor[1] == 0x07u);

    descriptor = usb_webhid_report_descriptor(&length);
    assert(descriptor != NULL);
    assert(length == USB_WEBHID_REPORT_DESCRIPTOR_BYTES);
    for(offset = 0u; offset < length; ++offset)
    {
        assert(descriptor[offset] != 0x85u); /* no Report ID item */
    }

    descriptor = usb_webhid_hid_descriptor(&length);
    assert(descriptor != NULL);
    assert(length == USB_WEBHID_HID_DESCRIPTOR_BYTES);
    assert(descriptor[6] == 0x22u);
    assert(descriptor[7] == USB_WEBHID_REPORT_DESCRIPTOR_BYTES);
    assert(descriptor[8] == 0u);
}

int main(void)
{
    test_protocol_layout();
    test_descriptors();
    return 0;
}
