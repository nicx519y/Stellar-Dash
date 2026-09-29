#ifndef XORA_USB_WEBHID_MEMORY_H
#define XORA_USB_WEBHID_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#if defined(__riscv)
#define USB_WEBHID_RAM __attribute__((section(".highcode"), noinline))
#else
#define USB_WEBHID_RAM
#endif
/* USB-only shared state: leave SPI DMA/ready interrupts serviceable. */
uint32_t usb_device_irq_save(void);
void usb_device_irq_restore(uint32_t enabled);
void *usb_webhid_copy(void *destination, const void *source, size_t length);
uint8_t usb_input_crc8(const uint8_t *data, uint8_t length);
void *usb_webhid_fill(void *destination, int value, size_t length);
#endif
