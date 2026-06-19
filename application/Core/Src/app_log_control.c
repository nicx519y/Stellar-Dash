#include "app_log_control.h"

#include <stdarg.h>
#include <stdio.h>

static volatile uint8_t s_app_serial_log_enabled = 0u;
static volatile uint8_t s_app_debug_log_enabled = 0u;

void AppLogControl_Set(uint8_t serial_enabled, uint8_t debug_enabled)
{
    s_app_serial_log_enabled = (serial_enabled != 0u) ? 1u : 0u;
    s_app_debug_log_enabled = (debug_enabled != 0u) ? 1u : 0u;
}

uint8_t AppLogControl_SerialEnabled(void)
{
    return s_app_serial_log_enabled;
}

uint8_t AppLogControl_DebugEnabled(void)
{
    return s_app_debug_log_enabled;
}

void AppLogControl_DebugPrintf(const char *prefix, const char *fmt, ...)
{
    va_list args;

    if((s_app_debug_log_enabled == 0u) || (fmt == 0))
    {
        return;
    }

    if(prefix != 0)
    {
        printf("%s", prefix);
    }
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\r\n");
}
