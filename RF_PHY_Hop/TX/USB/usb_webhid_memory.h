#ifndef XORA_USB_WEBHID_MEMORY_H
#define XORA_USB_WEBHID_MEMORY_H
#include <stddef.h>
#if defined(__riscv)
#define USB_WEBHID_RAM __attribute__((section(".highcode"), noinline))
#else
#define USB_WEBHID_RAM
#endif
void *usb_webhid_copy(void *destination, const void *source, size_t length);
void *usb_webhid_fill(void *destination, int value, size_t length);
#endif
