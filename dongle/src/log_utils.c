#include "log_utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ch585_usbhs_device.h"

/*
 * In XInput+CDC composite mode:
 * - EP2: XInput IN
 * - EP5: CDC Data IN
 */
#define CDC_LOG_EP DEF_UEP5
#define CDC_LOG_BUF_SIZE 96

void cdc_log_printf(const char *fmt, ...)
{
    char buf[CDC_LOG_BUF_SIZE];
    int len;
    uint8_t busy;
    va_list args;

    if ((fmt == 0) || (USBHS_DevEnumStatus == 0u)) {
        return;
    }

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len <= 0) {
        return;
    }
    if (len > (int)(sizeof(buf) - 1u)) {
        len = (int)(sizeof(buf) - 1u);
        buf[len] = '\0';
    }

    busy = (uint8_t)(USBHS_Endp_Busy[CDC_LOG_EP] & DEF_UEP_BUSY);
    if (busy != 0u) {
        return;
    }

    (void)USBHS_Endp_DataUp(CDC_LOG_EP, (uint8_t *)buf, (uint16_t)len, DEF_UEP_CPY_LOAD);
}

void cdc_log_alive_tick(uint32_t sec_counter)
{
    cdc_log_printf("[main] alive %lu s\r\n", (unsigned long)sec_counter);
}
