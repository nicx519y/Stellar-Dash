#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control/spi_screen_timed_popup.hpp"

extern "C" uint32_t HAL_GetTick(void);

static const char* kSocdLabels[] = {
    "Neutral",
    "Up Priority",
    "Second Input",
    "First Input",
    "Bypass",
};

static const uint32_t kSocdWarnAutoCloseMs = 5000u;
static const char* kSocdWarnLines[] = {
    "Competition",
    "SOCD must be Neutral"
};
static ScreenTimedPopup g_socdWarnPopup = {};
static bool g_socd_should_exit_after_confirm = true;

static GamepadProfile* default_profile(void) {
    return STORAGE_MANAGER.getDefaultGamepadProfile();
}

static uint8_t current_socd_index(void) {
    GamepadProfile* p = default_profile();
    if (!p) return 0;
    uint8_t v = (uint8_t)p->keysConfig.socdMode;
    if (v >= (uint8_t)NUM_SOCD_MODES) v = 0;
    return v;
}

static bool is_competition_profile_active(void) {
    GamepadProfile* p = default_profile();
    return p ? p->isCompetitionProfile : false;
}

static void show_socd_competition_warning(void) {
    ScreenTimedPopup_Show(
        &g_socdWarnPopup,
        "SOCD Locked",
        kSocdWarnLines,
        (uint8_t)(sizeof(kSocdWarnLines) / sizeof(kSocdWarnLines[0])),
        kSocdWarnAutoCloseMs,
        HAL_GetTick());
}

uint8_t ScreenDetailSocd_InitIndex(void) {
    ScreenTimedPopup_Reset(&g_socdWarnPopup);
    g_socd_should_exit_after_confirm = true;
    return current_socd_index();
}

void ScreenDetailSocd_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    if (ScreenTimedPopup_IsVisible(&g_socdWarnPopup)) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t)NUM_SOCD_MODES) idx = (int32_t)NUM_SOCD_MODES - 1;
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailSocd_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    ScreenTimedPopup_Update(&g_socdWarnPopup, HAL_GetTick());
    if (ScreenTimedPopup_IsVisible(&g_socdWarnPopup)) {
        ScreenTimedPopup_Render(&g_socdWarnPopup, lcd, style);
        return;
    }
    uint8_t selected = current_socd_index();
    ScreenDetailRender_List(lcd, "SOCD", kSocdLabels, (uint8_t)(sizeof(kSocdLabels) / sizeof(kSocdLabels[0])), index, selected, style);
}

void ScreenDetailSocd_OnConfirm(uint8_t index) {
    g_socd_should_exit_after_confirm = true;
    if (ScreenTimedPopup_IsVisible(&g_socdWarnPopup)) {
        ScreenTimedPopup_Close(&g_socdWarnPopup);
        g_socd_should_exit_after_confirm = false;
        return;
    }
    if (is_competition_profile_active() && index != (uint8_t)SOCDMode::SOCD_MODE_NEUTRAL) {
        show_socd_competition_warning();
        g_socd_should_exit_after_confirm = false;
        return;
    }
    GamepadProfile* p = default_profile();
    if (p && index < (uint8_t)NUM_SOCD_MODES) p->keysConfig.socdMode = (SOCDMode)index;
}

bool ScreenDetailSocd_ShouldExitAfterConfirm(void) {
    return g_socd_should_exit_after_confirm;
}
