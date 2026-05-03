#include "usb_hid_if.h"

#include <stdbool.h>
#include <string.h>

#include "platform_port.h"

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

__attribute__((weak))
bool usb_hw_ready(void)
{
    return true;
}

__attribute__((weak))
bool usb_hw_can_send_xinput(void)
{
    return true;
}

__attribute__((weak))
bool usb_hw_can_send_telemetry(void)
{
    return true;
}

__attribute__((weak))
bool usb_hw_send_xinput_report(const xinput_report_t *report)
{
    (void)report;
    return true;
}

__attribute__((weak))
bool usb_hw_send_telemetry_report(const uint8_t *payload, uint16_t len)
{
    (void)payload;
    (void)len;
    return true;
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

    /*
     * TODO: Replace with CH585 USB device init + XInput descriptor setup.
     * - Handle EP0 standard requests and return XInput descriptor sets.
     * - Configure IN endpoint 0x81 and OUT endpoint 0x02 interrupt transfer.
     * - Keep additional legacy XInput endpoints NAK-able for compatibility.
     */
}

void usb_hid_poll(void)
{
    telemetry_slot_t *slot;
    uint8_t next_tail;

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
