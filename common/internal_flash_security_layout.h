#ifndef HBOX_INTERNAL_FLASH_SECURITY_LAYOUT_H
#define HBOX_INTERNAL_FLASH_SECURITY_LAYOUT_H

#include <stdint.h>

/*
 * STM32H750xB internal user-Flash layout.
 *
 * The device has one 128 KiB erase sector.  The linker limits the bootloader
 * image to the first 112 KiB and reserves the final 16 KiB for append-only
 * security state.  Field code must never erase that sector: doing so would
 * erase the bootloader, identity, and anti-rollback state together.
 *
 * These addresses are a storage contract, not a claim that the factory
 * programming flow has passed hardware power-cut/lifecycle qualification.
 */
#define HBOX_INTERNAL_FLASH_BASE_ADDRESS             0x08000000u
#define HBOX_INTERNAL_FLASH_TOTAL_BYTES              0x00020000u

#define HBOX_BOOTLOADER_FLASH_ADDRESS                0x08000000u
#define HBOX_BOOTLOADER_FLASH_BYTES                  0x0001C000u

#define HBOX_DEVICE_IDENTITY_REGION_ADDRESS          0x0801C000u
#define HBOX_DEVICE_IDENTITY_REGION_BYTES            0x00001000u

#define HBOX_SECURITY_VERSION_REGION_ADDRESS         0x0801D000u
#define HBOX_SECURITY_VERSION_REGION_BYTES           0x00003000u

#define HBOX_INTERNAL_FLASH_PROGRAM_BYTES            32u
#define HBOX_DEVICE_IDENTITY_RECORD_BYTES            256u
#define HBOX_DEVICE_IDENTITY_COMMIT_BYTES            32u
#define HBOX_DEVICE_IDENTITY_SLOT_BYTES              288u
#define HBOX_DEVICE_IDENTITY_SLOT_COUNT              14u

#define HBOX_SECURITY_VERSION_RECORD_BYTES           32u
#define HBOX_SECURITY_VERSION_RECORD_COUNT           384u

#if defined(__cplusplus)
static_assert(
    HBOX_DEVICE_IDENTITY_REGION_ADDRESS ==
        HBOX_BOOTLOADER_FLASH_ADDRESS + HBOX_BOOTLOADER_FLASH_BYTES,
    "identity region must immediately follow bootloader");
static_assert(
    HBOX_SECURITY_VERSION_REGION_ADDRESS ==
        HBOX_DEVICE_IDENTITY_REGION_ADDRESS +
            HBOX_DEVICE_IDENTITY_REGION_BYTES,
    "security-version region overlaps identity region");
static_assert(
    HBOX_SECURITY_VERSION_REGION_ADDRESS +
            HBOX_SECURITY_VERSION_REGION_BYTES ==
        HBOX_INTERNAL_FLASH_BASE_ADDRESS +
            HBOX_INTERNAL_FLASH_TOTAL_BYTES,
    "security regions must end at internal-Flash boundary");
static_assert(
    HBOX_DEVICE_IDENTITY_SLOT_BYTES %
            HBOX_INTERNAL_FLASH_PROGRAM_BYTES ==
        0u,
    "identity slot must be flashword aligned");
static_assert(
    HBOX_DEVICE_IDENTITY_SLOT_COUNT *
            HBOX_DEVICE_IDENTITY_SLOT_BYTES <=
        HBOX_DEVICE_IDENTITY_REGION_BYTES,
    "identity slots exceed reserved region");
static_assert(
    HBOX_SECURITY_VERSION_RECORD_COUNT *
            HBOX_SECURITY_VERSION_RECORD_BYTES ==
        HBOX_SECURITY_VERSION_REGION_BYTES,
    "security-version record count does not fill its region");
#else
_Static_assert(
    HBOX_DEVICE_IDENTITY_REGION_ADDRESS ==
        HBOX_BOOTLOADER_FLASH_ADDRESS + HBOX_BOOTLOADER_FLASH_BYTES,
    "identity region must immediately follow bootloader");
_Static_assert(
    HBOX_SECURITY_VERSION_REGION_ADDRESS ==
        HBOX_DEVICE_IDENTITY_REGION_ADDRESS +
            HBOX_DEVICE_IDENTITY_REGION_BYTES,
    "security-version region overlaps identity region");
_Static_assert(
    HBOX_SECURITY_VERSION_REGION_ADDRESS +
            HBOX_SECURITY_VERSION_REGION_BYTES ==
        HBOX_INTERNAL_FLASH_BASE_ADDRESS +
            HBOX_INTERNAL_FLASH_TOTAL_BYTES,
    "security regions must end at internal-Flash boundary");
_Static_assert(
    HBOX_DEVICE_IDENTITY_SLOT_BYTES %
            HBOX_INTERNAL_FLASH_PROGRAM_BYTES ==
        0u,
    "identity slot must be flashword aligned");
_Static_assert(
    HBOX_DEVICE_IDENTITY_SLOT_COUNT *
            HBOX_DEVICE_IDENTITY_SLOT_BYTES <=
        HBOX_DEVICE_IDENTITY_REGION_BYTES,
    "identity slots exceed reserved region");
_Static_assert(
    HBOX_SECURITY_VERSION_RECORD_COUNT *
            HBOX_SECURITY_VERSION_RECORD_BYTES ==
        HBOX_SECURITY_VERSION_REGION_BYTES,
    "security-version record count does not fill its region");
#endif

#endif /* HBOX_INTERNAL_FLASH_SECURITY_LAYOUT_H */
