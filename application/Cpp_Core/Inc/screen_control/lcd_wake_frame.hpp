#pragma once
#include <stdint.h>

// First-frame acknowledgement after controller reinitialization. DMA IRQ alone
// is insufficient: the driver must finish all framebuffer chunks and SPI EOT.
class LcdWakeFrame {
public:
    void begin(uint32_t started) { since_ = started; submitted_ = false; }
    void submitted() { submitted_ = true; }
    int poll(uint32_t now, bool busy, bool done, bool error) const {
        if (error || now - since_ >= 1000u) return -1;
        return submitted_ && !busy && done ? 1 : 0;
    }
private:
    uint32_t since_ = 0u;
    bool submitted_ = false;
};
