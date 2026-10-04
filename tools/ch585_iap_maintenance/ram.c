#include <stdint.h>
#include <stddef.h>
#ifndef IAP_MAINTENANCE_HOST_TEST
#define RV_STATIC_INLINE static inline
#include "ISP585.h"
#endif

/* WCH-Link loads this helper only into RAM. No startup, SDK system-init,
 * configuration-word, EEPROM or protection operation is linked/called here. */
typedef struct IapMaintenanceControl {
    uint32_t magic, version, size, image_crc, old_crc, app_crc;
    uint32_t state, error, restored, nonce;
} IapMaintenanceControl;
#ifndef IAP_MAINTENANCE_HOST_TEST
#define MAINT_CONTROL ((volatile IapMaintenanceControl *)0x20018000u)
#define MAINT_IMAGE ((uint8_t *)0x20019000u)
#define MAINT_OLD ((uint8_t *)0x2001A000u)
#define MAINT_FLASH ((const uint8_t *)0u)
#define MAINT_CHIP_ID (*(volatile uint8_t *)0x40001041u)
#endif

static uint32_t maintenance_crc(const uint8_t *bytes, uint32_t count) {
    uint32_t crc = 0xFFFFFFFFu;
    while (count--) {
        crc ^= *bytes++;
        for (unsigned i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t maintenance_write(uint8_t *image) {
    uint32_t error = FLASH_ROM_ERASE(0u, 4096u);
    if (error) return error;
    /* Commit the entry/vector page last, after the rest passes readback. */
    error = FLASH_ROM_WRITE(256u, image + 256u, 3840u);
    if (!error) error = FLASH_ROM_VERIFY(256u, image + 256u, 3840u);
    if (!error) error = FLASH_ROM_WRITE(0u, image, 256u);
    if (!error) error = FLASH_ROM_VERIFY(0u, image, 4096u);
    return error;
}

void maintenance_main(void) {
    volatile IapMaintenanceControl *c = MAINT_CONTROL;
    const int initialize = c->magic == 0x4E494158u;
    if ((!initialize && c->magic != 0x50414958u) || c->version != 1u || c->size != 4096u ||
        c->state != 0u || !c->nonce || MAINT_CHIP_ID != 0x85u) {
        c->error = 1u; c->state = 3u; return;
    }
    if (maintenance_crc(MAINT_IMAGE, 4096u) != c->image_crc ||
        maintenance_crc(MAINT_FLASH, 4096u) != c->old_crc ||
        maintenance_crc(MAINT_OLD, 4096u) != c->old_crc ||
        maintenance_crc(MAINT_FLASH + 4096u, 0x6F000u) != c->app_crc) {
        c->error = 2u; c->state = 3u; return;
    }
    if (initialize) {
        /* Recheck every byte in RAM, not just CRC, before first provisioning. */
        for (uint32_t i = 0u; i < 0x70000u; ++i) {
            if (MAINT_FLASH[i] != 0xFFu || (i < 4096u && MAINT_OLD[i] != 0xFFu)) {
                c->error = 5u; c->state = 3u; return;
            }
        }
        const uint8_t marker[4] = {0xA9u, 0xBDu, 0xF9u, 0xF3u};
        for (unsigned i = 0u; i < 4u; ++i) {
            if (MAINT_IMAGE[0x14u + i] != marker[i]) {
                c->error = 3u; c->state = 3u; return;
            }
        }
    } else {
        /* Preserve the vendor startup marker verbatim; never provision options. */
        for (unsigned i = 0x14u; i < 0x18u; ++i) {
            if (MAINT_IMAGE[i] != MAINT_OLD[i]) {
                c->error = 3u; c->state = 3u; return;
            }
        }
    }
    c->state = 1u;
    c->error = maintenance_write(MAINT_IMAGE);
    if (c->error) {
        c->restored = maintenance_write(MAINT_OLD) == 0u ? 1u : 2u;
        c->state = 3u; return;
    }
    if (maintenance_crc(MAINT_FLASH + 4096u, 0x6F000u) != c->app_crc) {
        c->error = 4u; c->state = 3u; return;
    }
    c->state = 2u;
}
