#ifndef PLATFORM_PORT_H
#define PLATFORM_PORT_H

#include <stdbool.h>
#include <stdint.h>

void platform_clock_init(void);
void platform_gpio_init(void);
void platform_timer_init(void);

uint32_t platform_now_us(void);
void platform_led_set(bool on);
void platform_idle(void);

#endif /* PLATFORM_PORT_H */
