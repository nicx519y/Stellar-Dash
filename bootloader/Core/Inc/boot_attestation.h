#ifndef HBOX_BOOT_ATTESTATION_H
#define HBOX_BOOT_ATTESTATION_H

#include <stdbool.h>

#include "firmware_metadata.h"

bool BootAttestation_Prepare(const FirmwareMetadata *metadata);
void BootAttestation_Invalidate(void);

#endif
