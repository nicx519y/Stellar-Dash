#include "dongle_telemetry.h"

#include <string.h>

#include "dongle_fsm.h"
#include "report_pipeline.h"
#include "rf_link.h"
#include "usb_hid_if.h"

#define DONGLE_TLM_MAGIC (0x314E4D44u) /* "DMN1" */
#define DONGLE_TLM_INTERVAL_US (20000u) /* 50Hz side-channel */
#define DONGLE_TLM_FRAME_BYTES (32u)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t now_us;
    uint32_t rx_count;
    uint32_t tx_report_count;
    uint32_t invalid_count;
    uint32_t telemetry_drop_count;
    uint16_t stale_report_count;
    uint8_t latest_seq;
    uint8_t dongle_state;
    uint8_t flags; /* bit0 usb_ready, bit1 usb_can_send, bit2 rf_connected */
    uint8_t reserved0;
    uint16_t reserved1;
} dongle_tlm_frame_t;

typedef struct {
    uint32_t tx_report_count;
    uint16_t stale_report_count;
    uint32_t next_send_us;
    uint32_t last_report_sent_us;
} dongle_tlm_state_t;

static dongle_tlm_state_t s_tlm;

void dongle_telemetry_init(uint32_t now_us)
{
    memset(&s_tlm, 0, sizeof(s_tlm));
    s_tlm.next_send_us = now_us + DONGLE_TLM_INTERVAL_US;
}

void dongle_telemetry_on_report_sent(bool sent, bool used_neutral, uint32_t now_us)
{
    if (used_neutral) {
        s_tlm.stale_report_count++;
    }
    if (sent) {
        s_tlm.tx_report_count++;
        s_tlm.last_report_sent_us = now_us;
    }
}

void dongle_telemetry_tick(uint32_t now_us)
{
    dongle_tlm_frame_t frame;

    if ((int32_t)(now_us - s_tlm.next_send_us) < 0) {
        return;
    }
    s_tlm.next_send_us += DONGLE_TLM_INTERVAL_US;

    if (!usb_hid_ready()) {
        return;
    }

    memset(&frame, 0, sizeof(frame));
    frame.magic = DONGLE_TLM_MAGIC;
    frame.now_us = now_us;
    frame.rx_count = report_pipeline_rx_count();
    frame.tx_report_count = usb_hid_report_sent_count();
    frame.invalid_count = report_pipeline_invalid_count();
    frame.telemetry_drop_count = usb_hid_telemetry_drop_count();
    frame.stale_report_count = s_tlm.stale_report_count;
    frame.latest_seq = report_pipeline_latest_seq();
    frame.dongle_state = (uint8_t)dongle_fsm_get_state();
    frame.flags = (uint8_t)((usb_hid_ready() ? 0x01u : 0u) |
                            (usb_hid_can_send() ? 0x02u : 0u) |
                            (rf_link_is_connected() ? 0x04u : 0u));
    (void)usb_hid_try_send_telemetry((const uint8_t *)&frame, DONGLE_TLM_FRAME_BYTES);
}
