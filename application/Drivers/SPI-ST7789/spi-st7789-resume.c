#include "spi-st7789-resume.h"
#include <stddef.h>

// Cold boot powers PI9 long before the display driver starts. On resume the
// rail has just risen, and this PCB exposes no host-controlled LCD RESX pin.
// Allow the module's power/reset circuit to settle before the first command.
// This is a board margin, not an acknowledgement from the write-only panel.
enum { PANEL_POWER_SETTLE_MS = 120u };
static uint8_t phase;
static uint32_t deadline;

bool SPIST7789_ResumeActive(void) { return phase != 0u; }
void SPIST7789_CancelResume(void) { phase = 0u; }
void SPIST7789_BeginResume(uint32_t now) {
    SPIST7789_ResumePowerOn();
    phase = 1u;
    deadline = now + PANEL_POWER_SETTLE_MS;
}
// Same controller commands/delays as cold initialization; never blocks input.
int SPIST7789_PollResume(uint32_t now) {
    if (SPIST7789_IsReady()) return 1;
    if (phase == 0u) return -1;
    if ((int32_t)(now - deadline) < 0) return 0;
    bool ok = false;
    if (phase == 1u) {
        ok = SPIST7789_ResumePrepare() && SPIST7789_ResumeWrite(0x01, NULL, 0);
        if (ok) { phase = 2u; deadline = now + 150u; }
    } else if (phase == 2u) {
        ok = SPIST7789_ResumeWrite(0x11, NULL, 0);
        if (ok) { phase = 3u; deadline = now + 120u; }
    } else {
        const uint8_t colmod = 0x55u, madctl = 0xA0u;
        ok = SPIST7789_ResumeWrite(0x3A, &colmod, 1) &&
             SPIST7789_ResumeWrite(0x36, &madctl, 1) &&
             SPIST7789_ResumeWrite(0x21, NULL, 0) &&
             SPIST7789_ResumeWrite(0x13, NULL, 0) &&
             SPIST7789_ResumeWrite(0x29, NULL, 0);
        if (ok) { SPIST7789_ResumeReady(); phase = 0u; return 1; }
    }
    if (ok) return 0;
    SPIST7789_DeInit();
    return -1;
}
