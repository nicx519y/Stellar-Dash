#include <stdbool.h>
#include <stdint.h>

#include "dongle_config.h"
#include "dongle_fsm.h"
#include "platform_port.h"
#include "report_pipeline.h"
#include "rf_link.h"
#include "usb_hid_if.h"

static void try_flush_report(void)
{
    xinput_report_t report;
    uint32_t now_us;
    uint32_t last_rx_us;
    bool has_report;

    if (!dongle_fsm_allow_report()) {
        return;
    }

    if (!usb_hid_ready() || !usb_hid_can_send()) {
        return;
    }

    has_report = report_pipeline_get_latest(&report);
    now_us = platform_now_us();
    last_rx_us = report_pipeline_last_rx_us();

    if (!has_report || ((int32_t)(now_us - last_rx_us) > (int32_t)INPUT_STALE_TIMEOUT_US)) {
        report_pipeline_build_neutral(&report);
    }

    (void)usb_hid_try_send_report(&report);
}

int main(void)
{
    uint32_t next_report_us;

    platform_clock_init();
    platform_gpio_init();
    platform_timer_init();

    report_pipeline_init();
    usb_hid_init();
    rf_link_init(report_pipeline_on_radio_packet);
    dongle_fsm_init(platform_now_us());

    next_report_us = platform_now_us() + REPORT_INTERVAL_US;

    while (1) {
        uint32_t now = platform_now_us();

        rf_link_poll();
        dongle_fsm_tick(now);

        if ((int32_t)(now - next_report_us) >= 0) {
            next_report_us += REPORT_INTERVAL_US;
            try_flush_report();
        }

        platform_idle();
    }
}
