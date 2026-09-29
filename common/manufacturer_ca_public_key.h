#ifndef HBOX_MANUFACTURER_CA_PUBLIC_KEY_H
#define HBOX_MANUFACTURER_CA_PUBLIC_KEY_H

#include <stdint.h>

/*
 * Production generates a replacement header outside source control and
 * passes it with -include.  The all-zero repository fallback is deliberately
 * unusable so an unprovisioned build cannot authenticate a device.
 */
#ifndef HBOX_MANUFACTURER_CA_KEY_PROVISIONED
#define HBOX_MANUFACTURER_CA_KEY_PROVISIONED 0u
static const uint8_t HBOX_MANUFACTURER_CA_PUBLIC_KEY[65] = {0};
#endif

#endif
