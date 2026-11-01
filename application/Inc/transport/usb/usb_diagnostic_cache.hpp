#pragma once
#include <cstring>
#include "usb_diagnostics.h"

struct UsbDiagnosticCache {
    uint8_t bytes[XORA_USB_DIAG_BYTES];
    uint32_t sampledAtMs;
    uint32_t connectionGeneration;
    uint8_t length;
    bool valid;
    bool stale;

    bool update(bool received, const uint8_t *data, uint8_t size,
                uint8_t page, uint8_t index, uint32_t now) {
        const bool ok = received && data && size >= XORA_USB_DIAG_HEADER_BYTES &&
            size <= sizeof(bytes) && data[0] == XORA_USB_DIAG_VERSION &&
            data[1] == page && data[2] == index && data[3] + XORA_USB_DIAG_HEADER_BYTES == size;
        if (ok) {
            std::memcpy(bytes, data, size);
            length = size;
            sampledAtMs = now;
            connectionGeneration = usb_diag_get32(data + 8);
            valid = true;
        }
        stale = !ok;
        return ok;
    }
    UsbDiagnosticCache snapshot(uint32_t now) const {
        UsbDiagnosticCache result = *this;
        if (!valid || now - sampledAtMs > 1000u) result.stale = true;
        return result;
    }
};
