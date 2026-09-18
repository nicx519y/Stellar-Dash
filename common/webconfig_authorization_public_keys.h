#ifndef HBOX_WEBCONFIG_AUTHORIZATION_PUBLIC_KEYS_H
#define HBOX_WEBCONFIG_AUTHORIZATION_PUBLIC_KEYS_H

#include <stdint.h>

/*
 * Two slots permit current/next online authorization-key rotation.  A
 * generated production header must set the matching provisioned bit.
 */
#ifndef HBOX_WEBCONFIG_AUTH_KEY_PROVISIONED_MASK
#define HBOX_WEBCONFIG_AUTH_KEY_SLOT_COUNT 2u
#define HBOX_WEBCONFIG_AUTH_KEY_PROVISIONED_MASK 0u
static const uint8_t
    HBOX_WEBCONFIG_AUTHORIZATION_PUBLIC_KEYS
        [HBOX_WEBCONFIG_AUTH_KEY_SLOT_COUNT][65] = {{0}, {0}};
#endif

#endif
