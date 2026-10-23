#pragma once
#include <stdint.h>

/* Subdivision of the user-resource envelope. Firmware slots, the first
 * 64 KiB compatibility guard and CH585 staging retain their addresses. */
#define XORA_IMAGE_GUARD_BYTES 0x10000u
#define XORA_IMAGE_DATA_ADDR 0x90600000u
#define XORA_IMAGE_DATA_BYTES 0x170000u
#define BATTERY_TEST_STORAGE_ADDR 0x90770000u
#define BATTERY_TEST_STORAGE_SIZE 0x10000u

static inline int xora_image_range(uint32_t address, uint32_t bytes) {
    return address >= XORA_IMAGE_DATA_ADDR && address <= BATTERY_TEST_STORAGE_ADDR &&
        bytes <= BATTERY_TEST_STORAGE_ADDR - address;
}
