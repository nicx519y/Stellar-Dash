#include "screen_control/spi_screen_standby.hpp"
#include <cassert>

extern "C" uint32_t HAL_GetTick(void) { return 0; }
extern "C" void ST7789_FillScreen(ST7789_Handle*, uint32_t) {}
extern "C" void ST7789_DrawCircle(ST7789_Handle*, int, int, int, uint32_t) {}
extern "C" void ST7789_FillCircle(ST7789_Handle*, int, int, int, uint32_t) {}
extern "C" void ST7789_DrawBitmap(ST7789_Handle*, uint16_t, uint16_t, uint16_t,
    uint16_t, const void*, ST7789_BitmapFormat, uint32_t) {}

int main()
{
    ScreenStandby_Init(0, 0);
    ScreenStandby_Configure(2, "", 0, 0xffffff);
    ScreenStandby_Tick(10000);
    assert(ScreenStandby_IsActive());
    // A brief wake press is already released when LCD power-up finishes.
    ScreenStandby_Wake(60000, 0);
    ScreenStandby_NotifyInput(60000, 0, false);
    ScreenStandby_Tick(60000);
    assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(64999);
    assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(65000);
    assert(ScreenStandby_IsActive());
    // Repeat wake, including a wraparound of HAL's millisecond counter.
    ScreenStandby_Wake(0xfffffff0u, 0);
    ScreenStandby_Tick(0x10u);
    assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(0x1378u);
    assert(ScreenStandby_IsActive());
    // Reset the idle timer even when the screen saver wasn't active.
    ScreenStandby_Deactivate();
    ScreenStandby_Wake(100000, 0);
    ScreenStandby_Tick(100000);
    assert(!ScreenStandby_IsActive());

    // Game buttons neither postpone standby nor wake it. Their mask remains
    // available to redraw the pressed state in the standby button layout.
    ScreenStandby_Init(0, 0);
    ScreenStandby_NotifyInput(4000, 1u, false);
    ScreenStandby_Tick(4999);
    assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(5000);
    assert(ScreenStandby_IsActive());
    ScreenStandby_NotifyInput(6000, 0u, false);
    assert(ScreenStandby_IsActive());
    ScreenStandby_NotifyInput(6001, 1u, false);
    assert(ScreenStandby_IsActive());

    // Screen rotary/button activity restarts the timer and wakes standby.
    ScreenStandby_NotifyInput(6002, 1u, true);
    assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(11001);
    assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(11002);
    assert(ScreenStandby_IsActive());
}
