#include <stdbool.h>
#include <stdint.h>

#include "HAL.h"
#include "dongle_config.h"
#include "dongle_fsm.h"
#include "dongle_telemetry.h"
#include "platform_port.h"
#include "report_pipeline.h"
#include "rf_link.h"
#include "usb_hid_if.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if (DONGLE_DIAG_STAGE >= 4)
static void try_flush_report(void)
{
    xinput_report_t report;
    uint32_t now_us;
    uint32_t last_rx_us;
    bool has_report;
    bool use_neutral;
    bool sent;

    if (!dongle_fsm_allow_report()) {
        return;
    }

    if (!usb_hid_ready() || !usb_hid_can_send()) {
        return;
    }

    has_report = report_pipeline_get_latest(&report);
    now_us = platform_now_us();
    last_rx_us = report_pipeline_last_rx_us();
    use_neutral = (!has_report || ((int32_t)(now_us - last_rx_us) > (int32_t)INPUT_STALE_TIMEOUT_US));

    if (use_neutral) {
        report_pipeline_build_neutral(&report);
    }

    sent = usb_hid_try_send_report(&report);
    dongle_telemetry_on_report_sent(sent, use_neutral, now_us);
}
#endif

int main(void)
{
    uint32_t now_us;
    uint32_t next_toggle_us;
    bool led_on;
#if (DONGLE_DIAG_STAGE >= 4)
    uint32_t next_report_us = 0u;
#endif
#if (DONGLE_DIAG_STAGE >= 3)
    bool rf_started = false;
#endif

    platform_clock_init();
    platform_gpio_init();
    CH58x_BLEInit();
    HAL_Init();
    (void)RF_RoleInit();
    platform_timer_init();

    led_on = true;
    platform_led_set(led_on);
    next_toggle_us = platform_now_us() + 500000u;

#if (DONGLE_DIAG_STAGE >= 1)
    usb_hid_init();
#endif
#if (DONGLE_DIAG_STAGE >= 2)
    report_pipeline_init();
    dongle_fsm_init(platform_now_us());
    dongle_telemetry_init(platform_now_us());
#endif

    while (1) {
        platform_irq_ensure_enabled();
        TMOS_SystemProcess();
        now_us = platform_now_us();

        if ((int32_t)(now_us - next_toggle_us) >= 0) {
            next_toggle_us += 500000u;
            led_on = !led_on;
            platform_led_set(led_on);
        }

#if (DONGLE_DIAG_STAGE >= 1)
        usb_hid_poll();
#endif
#if (DONGLE_DIAG_STAGE >= 2)
        dongle_fsm_tick(now_us);
        dongle_telemetry_tick(now_us);
#endif
#if (DONGLE_DIAG_STAGE >= 3)
        if (!rf_started) {
            if (usb_hid_ready()) {
                rf_link_init(report_pipeline_on_radio_packet);
                rf_started = true;
            }
        }
#if (DONGLE_DIAG_RF_STEP >= 2)
        if (rf_started) {
            rf_link_poll();
        }
#endif
#endif
#if (DONGLE_DIAG_STAGE >= 4)
        if (next_report_us == 0u) {
            next_report_us = now_us + REPORT_INTERVAL_US;
        }
        if ((int32_t)(now_us - next_report_us) >= 0) {
            next_report_us += REPORT_INTERVAL_US;
            try_flush_report();
        }
#endif

#if DONGLE_DIAG_FORCE_LED_PATTERN
        /* 100ms fast blink: if this is not visible, current firmware likely not running. */
        platform_led_set((((now_us / 100000u) & 0x1u) == 0u));
#endif

        platform_idle();
    }
}
