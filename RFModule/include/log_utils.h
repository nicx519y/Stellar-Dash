#ifndef LOG_UTILS_H
#define LOG_UTILS_H

#include <stdint.h>

void log_uart_init(void);
void log_raw(const char *s);
void log_hex8(uint8_t v);

#endif /* LOG_UTILS_H */
