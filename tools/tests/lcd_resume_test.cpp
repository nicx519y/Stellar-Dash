#include "spi-st7789-resume.h"
#include "spi-st7789-backlight.h"
#include "screen_control/lcd_wake_frame.hpp"
#include <cassert>
#include <vector>
static bool ready, powered;
static unsigned failAt, calls;
static std::vector<unsigned> commands;
bool SPIST7789_ResumePrepare(void) { return ++calls != failAt; }
bool SPIST7789_ResumeWrite(uint8_t command, const uint8_t*, uint16_t) {
    commands.push_back(command); return ++calls != failAt;
}
void SPIST7789_ResumePowerOn(void) { powered = true; }
void SPIST7789_ResumeReady(void) { ready = true; }
bool SPIST7789_IsReady(void) { return ready; }
void SPIST7789_DeInit(void) { ready = powered = false; SPIST7789_CancelResume(); }
int main() {
    assert(SPIST7789_BacklightCompare(999u, 0u, true) == 1000u);
    assert(SPIST7789_BacklightCompare(999u, 100u, true) == 0u);
    assert(SPIST7789_BacklightCompare(999u, 0u, false) == 0u);
    assert(SPIST7789_BacklightCompare(999u, 100u, false) == 1000u);
    assert(SPIST7789_BacklightCompare(999u, 255u, true) == 0u);
    for (unsigned percent = 0; percent <= 100; ++percent) {
        assert(SPIST7789_BacklightCompare(999u, percent, true) +
               SPIST7789_BacklightCompare(999u, percent, false) == 1000u);
    }
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        SPIST7789_DeInit(); commands.clear();
        uint32_t start = cycle == 0 ? 0xfffffff0u : cycle * 1000u;
        SPIST7789_BeginResume(start); assert(powered && !ready);
        assert(SPIST7789_PollResume(start + 119) == 0 && commands.empty());
        assert(SPIST7789_PollResume(start + 120) == 0 && commands.back() == 1);
        assert(SPIST7789_PollResume(start + 269) == 0 && commands.size() == 1);
        assert(SPIST7789_PollResume(start + 270) == 0 && commands.back() == 0x11);
        assert(SPIST7789_PollResume(start + 389) == 0 && !ready);
        assert(SPIST7789_PollResume(start + 390) == 1 && ready);
        assert((commands == std::vector<unsigned>{1, 0x11, 0x3a, 0x36, 0x21, 0x13, 0x29}));
        LcdWakeFrame frame; frame.begin(start);
        assert(frame.poll(start + 390, false, true, false) == 0); // stale done
        frame.submitted();
        assert(frame.poll(start + 395, true, true, false) == 0); // chunks in flight
        assert(frame.poll(start + 400, false, false, false) == 0);
        assert(frame.poll(start + 405, false, true, false) == 1);
        assert(frame.poll(start + 405, false, true, true) == -1);
        assert(frame.poll(start + 1000, false, true, false) == -1);
    }
    for (unsigned operation = 1; operation <= 8; ++operation) {
        SPIST7789_DeInit(); failAt = operation; calls = 0;
        SPIST7789_BeginResume(0);
        int result = 0;
        for (uint32_t tick = 0; tick < 500 && result == 0; ++tick) result = SPIST7789_PollResume(tick);
        assert(result == -1 && !powered && !ready && !SPIST7789_ResumeActive());
    }
}
