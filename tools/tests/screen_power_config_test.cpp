#include "screen_control_json.hpp"
#include "screen_control/screen_deferred_save.hpp"
#include "screen_control/spi_screen_detail_pages.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "storagemanager.hpp"
#include "board_mode.hpp"
#include <cassert>
#include <cstdio>
#include <string>

StorageStub STORAGE_MANAGER;
BoardStub BOARD_MODE;
unsigned saves = 0;
std::string renderedTitle, renderedValue;
uint8_t renderedIndex;
void ScreenUI_RequestDeferredSave(uint32_t delay) { assert(delay == 2000); ++saves; }
void ScreenDetailRender_List(ST7789_Handle*, const char* title, const char* const*, uint8_t, uint8_t index, uint8_t, const ScreenUiStyle&, uint16_t, bool, int, uint32_t, uint32_t, const bool*) {
    renderedTitle = title; renderedIndex = index;
}
void ScreenDetailRender_Slider(ST7789_Handle*, const char* title, uint8_t index, const ScreenUiStyle&, uint8_t max, const char* value) {
    assert(max == 4); renderedTitle = title; renderedValue = value; renderedIndex = index;
}
static ScreenControlConfig legacy(uint8_t mode = 0) {
    ScreenControlConfig sc = {};
    sc.standbyDisplay = mode; sc.standbyTimeoutSeconds = 120;
    sc.featuresMask = 0xFFF; sc.currentPageId = 7;
    const uint8_t order[] = {3, 0, 1, 2, 11, 7, 5, 4, 6, 8, 9, 10};
    memcpy(sc.featuresOrder, order, sizeof(order));
    return sc;
}
static const char* patch(ScreenControlConfig& sc, const char* json, bool importing = false) {
    auto* parsed = cJSON_Parse(json); assert(parsed);
    const char* error = ScreenConfig::applyJson(sc, parsed, importing); cJSON_Delete(parsed); return error;
}
int main() {
    ScreenDeferredSave pending;
    unsigned attempts = 0;
    pending.request(0xfffffff0u, 2000u);
    auto fail = [&] { ++attempts; return false; };
    assert(pending.flush(0x10u, fail) == ScreenDeferredSave::Result::Idle);
    assert(pending.flush(1984u, fail) == ScreenDeferredSave::Result::Failed);
    assert(attempts == 1 && pending.pending() && pending.failed());
    assert(pending.flush(6983u, fail) == ScreenDeferredSave::Result::Idle);
    assert(pending.flush(6984u, [&] { ++attempts; return true; }) == ScreenDeferredSave::Result::Saved);
    assert(attempts == 2 && !pending.pending() && !pending.failed());
    for (uint8_t mode = 0; mode < 3; ++mode) {
        auto sc = legacy(mode); ScreenConfig::normalize(sc);
        assert(ScreenConfig::enabled(sc) == (mode != 0));
        assert(sc.standbyDisplay == mode && sc.standbyTimeoutSeconds == 120);
        assert(sc.currentPageId == 13 && (sc.featuresMask & (1u << 14)));
        const uint8_t expected[] = {3, 0, 1, 2, 11, 13, 8, 14, 9, 10, 255, 255};
        assert(!memcmp(sc.featuresOrder, expected, sizeof(expected)));
        auto copy = sc; ScreenConfig::normalize(sc); assert(!memcmp(&sc, &copy, sizeof(sc)));
        assert(!patch(sc, R"({"standbyEnabled":false})"));
        assert(sc.standbyDisplay == mode && sc.standbyTimeoutSeconds == 120);
        auto* encoded = ScreenConfig::toJson(sc);
        ScreenControlConfig restored = {}; assert(!ScreenConfig::applyJson(restored, encoded, true)); cJSON_Delete(encoded);
        assert(!ScreenConfig::enabled(restored) && restored.standbyDisplay == mode && restored.currentPageId == 13);
    }
    auto menuConfig = legacy(); ScreenConfig::normalize(menuConfig);
    uint8_t menuIds[12] = {};
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 10);
    for (unsigned i = 0; i < 10; ++i) assert(ScreenConfig::isTopLevel(menuIds[i]));
    assert(ScreenMain_FindMenuMeta(12)); // Maintenance remains restorable but never a normal menu.
    menuConfig.featuresMask &= ~(1u << 14);
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 9);
    BOARD_MODE.allowed = false;
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 8);
    BOARD_MODE.allowed = true;
    auto sc = legacy(); sc.featuresMask &= ~0xF0u; ScreenConfig::normalize(sc);
    assert(!(sc.featuresMask & (1u << 13)) && sc.currentPageId == 3);
    assert(!patch(sc, R"({"features":{"ledBrightnessAdjust":false,"ledEffectSwitch":true,"ambientBrightnessAdjust":false,"ambientEffectSwitch":false},"featuresOrder":["ambientEffectSwitch","screenBrightnessAdjust","inputModeSwitch"],"currentPageId":6,"standbyDisplay":"buttonLayout"})", true));
    assert(ScreenConfig::enabled(sc) && sc.currentPageId == 13 && sc.featuresOrder[0] == 13 && sc.featuresOrder[2] == 14);
    assert(!patch(sc, R"({"standbyDisplay":"none"})", true)); assert(!ScreenConfig::enabled(sc));
    assert(!patch(sc, R"({"standbyDisplay":"none","standbyEnabled":true})", true)); assert(!ScreenConfig::enabled(sc));
    const auto previous = sc;
    for (const char* invalid : {R"({"standbyEnabled":1})", R"({"standbyTimeoutSeconds":15})", R"({"standbyTimeoutSeconds":10.5})", R"({"standbyDisplay":"bogus"})", R"({"brightness":10,"features":{"power":1}})"}) {
        assert(patch(sc, invalid)); assert(!memcmp(&sc, &previous, sizeof(sc)));
    }
    assert(!patch(sc, R"({"features":{"webConfigEntry":false},"featuresOrder":["power","power","unknown"],"currentPageId":65535})"));
    assert(sc.featuresMask & SCREEN_FEATURE_WEB_CONFIG_ENTRY); assert(sc.currentPageId == 14);
    // Real detail dispatcher: group -> slider -> parent, switches independent of time.
    STORAGE_MANAGER.config.screenControl = sc;
    init_power_defaults(STORAGE_MANAGER.config.power);
    auto index = ScreenDetail_InitIndex(14); assert(index == 0);
    assert(!ScreenDetail_OnConfirm(14, index));
    ScreenDetail_OnRotate(14, &index, 127); assert(STORAGE_MANAGER.config.screenControl.standbyTimeoutSeconds == 300);
    assert(!ScreenConfig::enabled(STORAGE_MANAGER.config.screenControl));
    assert(!ScreenDetail_OnConfirm(14, index)); assert(ScreenConfig::enabled(STORAGE_MANAGER.config.screenControl));
    ScreenDetail_OnRotate(14, &index, -127); assert(STORAGE_MANAGER.config.screenControl.standbyTimeoutSeconds == 10);
    ScreenUiStyle style = {}; ST7789_Handle lcd = {};
    ScreenDetail_Render(&lcd, 14, index, style); assert(renderedTitle == "Screen Standby" && renderedValue == "10s ON");
    assert(!ScreenDetail_OnBack(14));
    ScreenDetail_OnRotate(14, &index, 1); assert(index == 1);
    assert(!ScreenDetail_OnConfirm(14, index));
    assert(!ScreenDetail_OnConfirm(14, index));
    assert(STORAGE_MANAGER.config.power.autoSleepEnabled == (HBOX_AUTO_SLEEP_ENABLED != 0));
    ScreenDetail_OnRotate(14, &index, -4);
    assert(STORAGE_MANAGER.config.power.autoStandbyMs == (HBOX_AUTO_SLEEP_ENABLED ? 10000u : 300000u));
    assert(!ScreenDetail_OnBack(14));
    ScreenDetail_Render(&lcd, 14, index, style); assert(renderedTitle == "Power" && renderedIndex == 1);
    assert(ScreenDetail_OnBack(14));
    index = ScreenDetail_InitIndex(13); ScreenDetail_OnRotate(13, &index, 1); assert(index == 1);
    assert(!ScreenDetail_OnConfirm(13, index)); assert(ScreenDetail_Kind(13) == SCREEN_DETAIL_LIST);
    assert(!ScreenDetail_OnConfirm(13, index)); // Effect confirmation returns to group.
    ScreenDetail_Render(&lcd, 13, index, style); assert(renderedTitle == "LED Setting" && renderedIndex == 1);
    assert(ScreenDetail_OnBack(13)); assert(saves >= 3);
    puts("screen config migration, JSON atomicity and grouped navigation passed");
}
