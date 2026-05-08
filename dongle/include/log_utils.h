#ifndef DONGLE_LOG_UTILS_H
#define DONGLE_LOG_UTILS_H

#include <stdint.h>

void cdc_log_printf(const char *fmt, ...);
void cdc_log_alive_tick(uint32_t sec_counter);

#endif
