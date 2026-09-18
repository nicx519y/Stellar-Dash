#ifndef HBOX_WEBHID_TELEMETRY_HOOK_H
#define HBOX_WEBHID_TELEMETRY_HOOK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Called at the authoritative Hall-button state transition point.  The
 * implementation is a no-op unless an authenticated performance subscription
 * is active.
 */
void WebHidTelemetry_OnAdcTransition(uint8_t button_index,
                                     uint8_t pressed);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_WEBHID_TELEMETRY_HOOK_H */
