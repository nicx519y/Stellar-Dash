#ifndef HBOX_FIRMWARE_SIGNATURE_H
#define HBOX_FIRMWARE_SIGNATURE_H

#include <stdbool.h>
#include <stdint.h>

#include "firmware_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical metadata is the packed FirmwareMetadata byte sequence with the
 * transport CRC, firmware_hash and signature fields set to zero.  Everything
 * else, including slot addresses and security policy bytes, is authenticated.
 */
bool firmware_metadata_calculate_hash(const FirmwareMetadata* metadata,
                                      uint8_t hash[32]);
bool firmware_release_key_is_provisioned(void);
FirmwareValidationResult firmware_metadata_verify_signature(
    const FirmwareMetadata* metadata);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_FIRMWARE_SIGNATURE_H */
