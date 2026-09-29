#include "usb_webhid_memory.h"
#include "CH58x_common.h"

USB_WEBHID_RAM uint32_t usb_device_irq_save(void)
{
    const uint32_t enabled=PFIC_GetStatusIRQ(USB2_DEVICE_IRQn);
    PFIC_DisableIRQ(USB2_DEVICE_IRQn);
    return enabled;
}

USB_WEBHID_RAM void usb_device_irq_restore(uint32_t enabled)
{
    if(enabled)PFIC_EnableIRQ(USB2_DEVICE_IRQn);
}

/* The SDK nano-libc byte loops execute from Flash. Keep bounded report/ring
 * copies in SRAM, including unaligned slices; volatile prevents GCC from
 * replacing these loops with calls back into memcpy/memset. */
USB_WEBHID_RAM void *usb_webhid_copy(void *destination, const void *source, size_t length)
{
    volatile unsigned char *out = (volatile unsigned char *)destination;
    const volatile unsigned char *in = (const volatile unsigned char *)source;
    while(length--) *out++ = *in++;
    return destination;
}
USB_WEBHID_RAM void *usb_webhid_fill(void *destination, int value, size_t length)
{
    volatile unsigned char *out = (volatile unsigned char *)destination;
    while(length--) *out++ = (unsigned char)value;
    return destination;
}

/* Same polynomial/initial value as usb_board_input_crc8. Keep the nibble
 * table in initialized SRAM, not Flash, on the 8 kHz input path. */
USB_WEBHID_RAM uint8_t usb_input_crc8(const uint8_t *data, uint8_t length)
{
    static volatile uint8_t table[16] = {
        0x00,0x07,0x0e,0x09,0x1c,0x1b,0x12,0x15,
        0x38,0x3f,0x36,0x31,0x24,0x23,0x2a,0x2d
    };
    uint8_t crc = 0u;
    if(data == 0) return 0u;
    while(length--) {
        crc ^= *data++;
        crc = (uint8_t)((crc << 4) ^ table[crc >> 4]);
        crc = (uint8_t)((crc << 4) ^ table[crc >> 4]);
    }
    return crc;
}
