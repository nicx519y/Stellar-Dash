#ifndef HBOX_BOOT_FIRMWARE_SECURITY_H
#define HBOX_BOOT_FIRMWARE_SECURITY_H

#include <stdbool.h>

#include "dual_slot_config.h"
#include "security_version_journal.h"

#ifdef __cplusplus
extern "C" {
#endif

FirmwareValidationResult FirmwareSecurity_ValidateMetadata(
    const FirmwareMetadata* metadata);
bool FirmwareSecurity_ValidateSlot(const FirmwareMetadata* metadata,
                                   FirmwareSlot slot);

/*
 * Call only after FirmwareSecurity_ValidateSlot() has accepted the complete
 * signed slot.  This commits the package security version before execution.
 */
bool FirmwareSecurity_CommitValidatedSecurityVersion(
    const FirmwareMetadata* metadata);

hbox_security_version_status_t
FirmwareSecurity_LastSecurityVersionStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_BOOT_FIRMWARE_SECURITY_H */
