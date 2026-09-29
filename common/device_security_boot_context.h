#ifndef HBOX_DEVICE_SECURITY_BOOT_CONTEXT_H
#define HBOX_DEVICE_SECURITY_BOOT_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#include "device_security_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reset-stage -> application handoff.  SRAM4 is not initialised by either
 * linker script, so the final 1 KiB can carry an attested, per-boot signing
 * key across the direct bootloader jump.  It is never accepted after a reset
 * unless the whole structure and CRC are valid.
 */
#define HBOX_BOOT_CONTEXT_ADDRESS       0x3800FC00u
#define HBOX_BOOT_CONTEXT_MAGIC         0x31434248u /* "HBC1" */
#define HBOX_BOOT_CONTEXT_VERSION       1u
#define HBOX_BOOT_PRIVATE_KEY_BYTES     32u
#define HBOX_BOOT_CONTEXT_MAX_BYTES     1024u

#if defined(__GNUC__)
#define HBOX_BOOT_CONTEXT_PACKED __attribute__((packed))
#else
#define HBOX_BOOT_CONTEXT_PACKED
#endif

typedef struct HBOX_BOOT_CONTEXT_PACKED
{
    uint32_t magic_le;
    uint8_t version;
    uint8_t reserved0;
    uint16_t total_bytes_le;
    uint32_t crc32_le;
    uint32_t created_at_tick_le;
    uint8_t boot_private_key[HBOX_BOOT_PRIVATE_KEY_BYTES];
    hbox_device_certificate_v1_t device_certificate;
    hbox_boot_attestation_v1_t boot_attestation;
    char firmware_version[16];
    uint8_t reserved[15];
} hbox_boot_security_context_v1_t;

#define HBOX_BOOT_CONTEXT_CRC_OFFSET \
    ((uint16_t)offsetof(hbox_boot_security_context_v1_t, crc32_le))

uint32_t HBoxSecurity_Crc32Skipping(const uint8_t *data,
                                    size_t length,
                                    size_t skip_offset,
                                    size_t skip_size);
void HBoxSecurity_InvalidateBootContext(void);
int HBoxSecurity_ValidateBootContext(
    const hbox_boot_security_context_v1_t *context);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_DEVICE_SECURITY_BOOT_CONTEXT_H */
