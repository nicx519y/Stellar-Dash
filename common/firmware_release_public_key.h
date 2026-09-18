#ifndef HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_H
#define HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_H

#include <stdint.h>

/*
 * A production build must force-include a generated provisioning header which
 * defines both symbols below before this fallback is parsed.  Keeping the
 * repository fallback unprovisioned makes an accidental production image fail
 * closed instead of silently trusting a development key.
 */
#ifndef HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED
#define HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED 0u
static const uint8_t hbox_firmware_release_public_key[65] = {0u};
#endif

#endif /* HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_H */
