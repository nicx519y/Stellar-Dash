#ifndef HBOX_BOOT_SECURITY_VERSION_STORE_H
#define HBOX_BOOT_SECURITY_VERSION_STORE_H

#include <stdint.h>

#include "security_version_journal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Production provider contract.
 *
 * The default repository build intentionally has
 * HBOX_SECURITY_VERSION_PROVIDER_READY=0 and returns UNAVAILABLE.  A vetted
 * production provider is injected as an additional source through the
 * bootloader Makefile and implements the two Provider_* functions below.
 *
 * Load must authenticate/protect the value against rollback and modification.
 * Advance must be power-loss safe and must never reduce the stored value.
 * Providers backed by append-only record storage may use
 * common/security_version_journal.*, while a secure element may expose its
 * native monotonic counter directly.
 */
hbox_security_version_status_t
HBoxSecurityVersionProvider_Load(uint32_t *minimum_version);

hbox_security_version_status_t
HBoxSecurityVersionProvider_Advance(uint32_t requested_minimum_version);

hbox_security_version_status_t
SecurityVersionStore_Load(uint32_t *minimum_version);

hbox_security_version_status_t
SecurityVersionStore_Advance(uint32_t requested_minimum_version);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_BOOT_SECURITY_VERSION_STORE_H */
