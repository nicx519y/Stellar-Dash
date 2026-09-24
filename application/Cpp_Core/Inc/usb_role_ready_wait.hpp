#ifndef USB_ROLE_READY_WAIT_HPP
#define USB_ROLE_READY_WAIT_HPP

#include <stdint.h>

namespace UsbRoleReady {

/* Passive observation only: no SPI clocks while the selector hands off.
 * readFrame must already have witnessed ACK release. Otherwise an initial
 * low level could still be the ACK, so retain the full legacy settle time.
 * CH585 emits no asynchronous USB event before GET_CAPS. A subsequent low
 * then high therefore identifies its Application-ready pulse. Missing that
 * pulse is harmless: keep the same 150-ms fallback and validate CAPS later. */
template <typename Tick, typename High, typename Delay>
bool wait(bool ackReleased, Tick tick, High high, Delay delay)
{
    constexpr uint32_t kFallbackMs = 150u;
    constexpr uint32_t kReleaseGuardMs = 2u;
    if (!ackReleased) {
        delay(kFallbackMs);
        return false;
    }

    const uint32_t started = tick();
    uint32_t releasedAt = 0u;
    bool sawLow = false;
    bool qualifyingHigh = false;
    while (static_cast<uint32_t>(tick() - started) < kFallbackMs) {
        const uint32_t now = tick();
        if (!high()) {
            sawLow = true;
            qualifyingHigh = false;
        } else if (sawLow) {
            if (!qualifyingHigh) {
                releasedAt = now;
                qualifyingHigh = true;
            } else if (static_cast<uint32_t>(now - releasedAt) >= kReleaseGuardMs) {
                return true;
            }
        }
        delay(1u);
    }
    return false;
}

} // namespace UsbRoleReady

#endif
