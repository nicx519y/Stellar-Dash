#include <stdbool.h>
#include <stdint.h>

#include "dongle_config.h"
#include "log_utils.h"
#include "platform_port.h"
#include "usb_hid_if.h"

int main(void)
{
    uint32_t now_us;
    uint32_t next_toggle_us;
    uint32_t next_alive_us = 0u;
    uint32_t alive_sec = 0u;
    bool led_on;
    bool cdc_boot_banner_sent = false;

    platform_clock_init();
    platform_gpio_init();
    platform_timer_init();
    cdc_log_printf("[boot] clock/gpio/timer ok\r\n");

    led_on = true;
    platform_led_set(led_on);
    next_toggle_us = platform_now_us() + 500000u;

    usb_hid_init();
    cdc_log_printf("[boot] USB full-speed XInput+CDC init\r\n");

    while (1) {
        platform_irq_ensure_enabled();
        now_us = platform_now_us();

        if ((int32_t)(now_us - next_toggle_us) >= 0) {
            next_toggle_us += 500000u;
            led_on = !led_on;
            platform_led_set(led_on);
        }

        usb_hid_poll();
        if (!cdc_boot_banner_sent && usb_hid_ready()) {
            cdc_boot_banner_sent = true;
            cdc_log_printf("[boot] XInput+CDC ready\r\n");
        }

        if (next_alive_us == 0u) {
            next_alive_us = now_us + 1000000u;
        }
        if ((int32_t)(now_us - next_alive_us) >= 0) {
            next_alive_us += 1000000u;
            alive_sec++;
            cdc_log_alive_tick(alive_sec);
        }

#if DONGLE_DIAG_FORCE_LED_PATTERN
        /* 100ms fast blink: if this is not visible, current firmware likely not running. */
        platform_led_set((((now_us / 100000u) & 0x1u) == 0u));
#endif

        platform_idle();
    }
}
