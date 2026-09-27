#include "usb_webhid_memory.h"

/* The SDK nano-libc byte loops execute from Flash. Keep bounded report/ring
 * copies in SRAM, including unaligned slices; volatile prevents GCC from
 * replacing these loops with calls back into memcpy/memset. */
USB_WEBHID_RAM void *usb_webhid_copy(void *destination, const void *source, size_t length)
{
    volatile unsigned char *out = destination;
    const volatile unsigned char *in = source;
    while(length--) *out++ = *in++;
    return destination;
}
USB_WEBHID_RAM void *usb_webhid_fill(void *destination, int value, size_t length)
{
    volatile unsigned char *out = destination;
    while(length--) *out++ = (unsigned char)value;
    return destination;
}
