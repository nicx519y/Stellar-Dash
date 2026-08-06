#ifndef HBOX_SECURE_ACCESS_HANDOFF_H
#define HBOX_SECURE_ACCESS_HANDOFF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    HBOX_SECURE_ACCESS_OK = 0,
    HBOX_SECURE_ACCESS_DEVICE_ID_MISMATCH,
    HBOX_SECURE_ACCESS_SILICON_REVISION_MISMATCH,
    HBOX_SECURE_ACCESS_STANDARD_MODE,
    HBOX_SECURE_ACCESS_RDP_NOT_LEVEL_1,
    HBOX_SECURE_ACCESS_AREA_MISMATCH,
    HBOX_SECURE_ACCESS_NOT_EXECUTING_INTERNAL,
    HBOX_SECURE_ACCESS_INVALID_VECTOR,
    HBOX_SECURE_ACCESS_INVALID_RSS
} hbox_secure_access_status_t;

/*
 * Validates the immutable lifecycle assumptions required before Kdev may be
 * used.  This routine never programs option bytes.  Production provisioning
 * owns SECURITY/RDP; the first secure boot asks the STM32 ROM RSS to create
 * the secure area, and all later boots only verify it.
 */
hbox_secure_access_status_t HBoxSecureAccess_ValidateLifecycle(void);

/*
 * The H750 RSS owns the one-time transition from a blank secure-area
 * definition to a protected internal-Flash boot region.  This predicate is
 * deliberately narrow: SECURITY and RDP1 must already be active, execution
 * must be inside internal Flash, and SCAR must still describe an empty area.
 */
int HBoxSecureAccess_CanInitializeFullInternalFlashArea(void);

void HBoxSecureAccess_InitializeFullInternalFlashArea(void)
    __attribute__((noreturn));

const char *HBoxSecureAccess_StatusString(
    hbox_secure_access_status_t status);

/*
 * Close the STM32H750 secure user area through the ROM RSS service and transfer
 * to an application vector table.  The caller must already be privileged,
 * have interrupts disabled, and have disabled the MPU.  A successful call
 * never returns.
 */
void HBoxSecureAccess_ExitToApplication(uint32_t vector_table)
    __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* HBOX_SECURE_ACCESS_HANDOFF_H */
