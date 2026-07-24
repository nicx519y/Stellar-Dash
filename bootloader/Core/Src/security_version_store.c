#include "security_version_store.h"

#include "firmware_metadata.h"

#ifndef HBOX_SECURITY_VERSION_PROVIDER_READY
#define HBOX_SECURITY_VERSION_PROVIDER_READY 0
#endif

hbox_security_version_status_t
SecurityVersionStore_Load(uint32_t *minimum_version)
{
#if HBOX_SECURITY_VERSION_PROVIDER_READY
    hbox_security_version_status_t status;

    if (minimum_version == NULL) {
        return HBOX_SECURITY_VERSION_UNAVAILABLE;
    }
    *minimum_version = 0u;
    status = HBoxSecurityVersionProvider_Load(minimum_version);
    if (status != HBOX_SECURITY_VERSION_OK) {
        *minimum_version = 0u;
        return status;
    }
    if (*minimum_version < FIRMWARE_SECURITY_VERSION) {
        *minimum_version = 0u;
        return HBOX_SECURITY_VERSION_CORRUPT;
    }
    return HBOX_SECURITY_VERSION_OK;
#else
    if (minimum_version != NULL) {
        *minimum_version = 0u;
    }
    return HBOX_SECURITY_VERSION_UNAVAILABLE;
#endif
}

hbox_security_version_status_t
SecurityVersionStore_Advance(uint32_t requested_minimum_version)
{
#if HBOX_SECURITY_VERSION_PROVIDER_READY
    uint32_t before = 0u;
    uint32_t after = 0u;
    hbox_security_version_status_t status =
        SecurityVersionStore_Load(&before);

    if (status != HBOX_SECURITY_VERSION_OK) {
        return status;
    }
    if (requested_minimum_version < before) {
        return HBOX_SECURITY_VERSION_ROLLBACK;
    }
    if (requested_minimum_version == before) {
        return HBOX_SECURITY_VERSION_OK;
    }

    status = HBoxSecurityVersionProvider_Advance(
        requested_minimum_version);
    if (status != HBOX_SECURITY_VERSION_OK) {
        return status;
    }
    status = SecurityVersionStore_Load(&after);
    if (status != HBOX_SECURITY_VERSION_OK) {
        return status;
    }
    /*
     * A provider advancing farther than requested is valid monotonic state,
     * but this candidate must no longer boot because its version is now below
     * the authoritative minimum.
     */
    return after == requested_minimum_version
               ? HBOX_SECURITY_VERSION_OK
               : HBOX_SECURITY_VERSION_ROLLBACK;
#else
    (void)requested_minimum_version;
    return HBOX_SECURITY_VERSION_UNAVAILABLE;
#endif
}
