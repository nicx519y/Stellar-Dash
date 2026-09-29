#ifndef HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER_H
#define HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER_H

#include <stdint.h>

#include "security_version_journal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * In-tree STM32H750 internal-Flash provider.
 *
 * Load is read-only. Advance appends one 32-byte Flash word after validating
 * the secure lifecycle. Neither operation erases the H750xB's single 128 KiB
 * user-Flash sector.
 */
hbox_security_version_status_t
HBoxSecurityVersionProvider_Load(uint32_t *minimum_version);

hbox_security_version_status_t
HBoxSecurityVersionProvider_Advance(uint32_t requested_minimum_version);

/*
 * Factory-only initialization used by FactoryIdentityEnrollment_Install().
 * The implementation is compiled only when the explicit factory build gate
 * is enabled and independently requires the runtime factory authorization
 * hook plus the same secure lifecycle as Advance().
 */
hbox_security_version_status_t
HBoxSecurityVersionInternalFlash_ProvisionFactory(
    uint32_t initial_minimum_version);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER_H */
