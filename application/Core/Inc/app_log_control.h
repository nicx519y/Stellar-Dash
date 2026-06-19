#ifndef APP_LOG_CONTROL_H
#define APP_LOG_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void AppLogControl_Set(uint8_t serial_enabled, uint8_t debug_enabled);
uint8_t AppLogControl_SerialEnabled(void);
uint8_t AppLogControl_DebugEnabled(void);
void AppLogControl_DebugPrintf(const char *prefix, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
