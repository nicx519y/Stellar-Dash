#include "screen_control/spi_screen_standby.hpp"
#include <cassert>

static uint32_t tick = 0;
static unsigned draws = 0;
extern "C" uint32_t HAL_GetTick(void) { return tick; }
extern "C" void ST7789_FillScreen(ST7789_Handle*, uint32_t) { ++draws; }
extern "C" void ST7789_DrawCircle(ST7789_Handle*, int, int, int, uint32_t) {}
extern "C" void ST7789_FillCircle(ST7789_Handle*, int, int, int, uint32_t) {}
extern "C" void ST7789_DrawBitmap(ST7789_Handle*, uint16_t, uint16_t, uint16_t,
    uint16_t, const void*, ST7789_BitmapFormat, uint32_t) {}

bool ScreenJpeg_Begin(const uint8_t*, uint32_t, ST7789_Handle*) { return false; }
void ScreenJpeg_Cancel() {}

int main() {
    ScreenStandby_Init(0, 0);
    ScreenStandby_Configure(2, 10, "", 0, 0xffffff, true);
    ScreenStandby_Tick(10000); assert(ScreenStandby_IsActive());
    ScreenStandby_Wake(60000, 0);
    ScreenStandby_Tick(69999); assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(70000); assert(ScreenStandby_IsActive());
    ScreenStandby_Wake(0xfffffff0u, 0);
    ScreenStandby_Tick(0x10u); assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(0x2700u); assert(ScreenStandby_IsActive());
    // Off still preserves the selected layout/time.
    tick = 80000;
    ScreenStandby_Configure(2, 10, "", 0, 0xffffff, false);
    assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(1000000); assert(!ScreenStandby_IsActive());
    tick = 1000000;
    ScreenStandby_Configure(0, 10, "", 0, 0xffffff, true);
    ScreenStandby_NotifyInput(tick + 9000, 1, false);
    ScreenStandby_Tick(tick + 10000); assert(ScreenStandby_IsActive());
    // Game buttons do not wake screen-off; the screen renderer remains untouched.
    ScreenStandby_NotifyInput(tick + 11000, 0, false); assert(ScreenStandby_IsActive());
    ST7789_Handle lcd = {}; draws = 0; ScreenStandby_Render(&lcd, 0); assert(draws == 0);
    ScreenStandby_NotifyInput(tick + 12000, 0, true); assert(!ScreenStandby_IsActive());
    // A changed timeout restarts the independent idle timer.
    tick += 15000;
    ScreenStandby_Configure(0, 30, "", 0, 0xffffff, true);
    ScreenStandby_Tick(tick + 29999); assert(!ScreenStandby_IsActive());
    ScreenStandby_Tick(tick + 30000); assert(ScreenStandby_IsActive());
    tick += 31000;
    ScreenStandby_Configure(2, 10, "", 0, 0xffffff, true);
    ScreenStandby_Tick(tick + 10000); assert(ScreenStandby_IsActive());
    ScreenStandby_NotifyInput(tick + 10001, 1, false); assert(ScreenStandby_IsActive());
    ScreenStandby_Render(&lcd, 1); assert(draws > 0);
    // Missing images never result in an active blank image saver.
    tick += 12000;
    ScreenStandby_Configure(1, 10, "", 0, 0xffffff, true);
    ScreenStandby_Tick(tick + 10000); assert(!ScreenStandby_IsActive());
}
