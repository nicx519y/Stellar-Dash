#include "screen_control_json.hpp"
#include "screen_control/screen_deferred_save.hpp"
#include "screen_control/spi_screen_detail_pages.hpp"
#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control/spi_screen_ui_common.hpp"
#include "storagemanager.hpp"
#include "board_mode.hpp"
#include <cassert>
#include <cstdio>
#include <string>

StorageStub STORAGE_MANAGER;
BoardStub BOARD_MODE;
unsigned saves = 0;
uint32_t tick = 0;
extern "C" uint32_t HAL_GetTick(void) { return tick; }
bool arrowUp = false, arrowDown = false;
void ST7789_FillRect(ST7789_Handle*, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t) {
    assert(x + w <= ST7789_WIDTH && y + h <= ST7789_HEIGHT);
    if (x < ST7789_WIDTH - SPI_SCREEN_RIGHT_BAR_W) return;
    if (y < ST7789_HEIGHT / 3u) {
        if (w == SPI_SCREEN_RIGHT_BAR_W) arrowUp = false;
        else if (h == 1) arrowUp = true;
    }
    if (y >= (ST7789_HEIGHT / 3u) * 2u) {
        if (w == SPI_SCREEN_RIGHT_BAR_W) arrowDown = false;
        else if (h == 1) arrowDown = true;
    }
}
void ST7789_DrawString(ST7789_Handle*, uint16_t, uint16_t, const char*, uint32_t, uint32_t, uint8_t) {}

std::string renderedTitle, renderedValue;
uint8_t renderedIndex;
void ScreenUI_RequestDeferredSave(uint32_t delay) { assert(delay == 2000); ++saves; }
void ScreenDetailRender_List(ST7789_Handle*, const char* title, const char* const*, uint8_t, uint8_t index, uint8_t, const ScreenUiStyle&, uint16_t, bool, int, uint32_t, uint32_t, const bool*, bool) {
    renderedTitle = title; renderedIndex = index;
}
void ScreenDetailRender_Slider(ST7789_Handle*, const char* title, uint8_t index, const ScreenUiStyle&, uint8_t max, const char* value) {
    assert(max == 4 || max == 100); renderedTitle = title; renderedValue = value ? value : ""; renderedIndex = index;
}
void ScreenDetailRender_TitleLines(ST7789_Handle*, const char* title, const char* const* lines, uint8_t count, const ScreenUiStyle&) {
    renderedTitle=title; renderedValue=count?lines[0]:"";
}
static unsigned batteryStarts=0,batteryEnds=0,chargeStarts=0;
extern "C" void ChargeTest_RequestStart() { ++chargeStarts; }
extern "C" int BatteryTest_IsCharge() { return chargeStarts>0; }
extern "C" void BatteryTest_RequestStart() { ++batteryStarts; }
extern "C" void BatteryTest_RequestEnd() { ++batteryEnds; }
extern "C" const char *BatteryTest_Status() { return "Recording"; }
extern "C" uint32_t BatteryTest_Elapsed() { return 123000; }
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
    // The new RAM-only child needs an explicit start confirmation and no config save.
    {
        ScreenUiStyle style={}; ST7789_Handle lcd={};
        auto cursor=ScreenDetail_InitIndex(ScreenConfig::kPower);
        ScreenDetail_OnRotate(ScreenConfig::kPower,&cursor,2);
        assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower,cursor)); // child
        assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower,cursor)); // warning
        assert(batteryStarts==0);
        ScreenDetail_Render(&lcd,ScreenConfig::kPower,cursor,style);
        assert(renderedTitle=="Start Battery Test");
        assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower,cursor));
        assert(batteryStarts==1 && saves==0);
        assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower,cursor)); // status -> list
        ScreenDetail_OnRotate(ScreenConfig::kPower,&cursor,2);
        assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower,cursor)); // end
        assert(batteryEnds==1 && saves==0);
        assert(!ScreenDetail_OnBack(ScreenConfig::kPower)); // status -> list
        assert(!ScreenDetail_OnBack(ScreenConfig::kPower)); // group
    }
    auto defaults = legacy();
    ScreenConfig::setDefaultOrder(defaults);
    ScreenConfig::normalize(defaults);
    const uint8_t defaultOrder[] = {15, 3, 0, 1, 2, 11, 13, 8, 14, 9, 10, 255};
    assert(!memcmp(defaults.featuresOrder, defaultOrder, sizeof(defaultOrder)));
    // Explicit user order remains authoritative after normalization.
    auto custom = defaults;
    custom.featuresOrder[0] = 3;
    custom.featuresOrder[1] = 15;
    ScreenConfig::normalize(custom);
    assert(custom.featuresOrder[0] == 3 && custom.featuresOrder[1] == 15);
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
        const uint8_t expected[] = {3, 0, 1, 2, 11, 13, 8, 14, 9, 10, 15, 255};
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
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 11);
    for (unsigned i = 0; i < 11; ++i) assert(ScreenConfig::isTopLevel(menuIds[i]));
    assert(ScreenMain_FindMenuMeta(12)); // Maintenance remains restorable but never a normal menu.
    menuConfig.featuresMask &= ~(1u << 14);
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 10);
    BOARD_MODE.allowed = false;
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 9);
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
    // Both group lists stop at their ends, including accumulated encoder steps.
    for (const uint8_t menu : {ScreenConfig::kLed, ScreenConfig::kPower}) {
        const uint8_t last = 3; // Power includes the independent Charge Test child.
        auto cursor = ScreenDetail_InitIndex(menu);
        ScreenUiStyle groupStyle = {}; ST7789_Handle groupLcd = {};
        auto expectCursor = [&](uint8_t expected) {
            assert(cursor == expected);
            ScreenDetail_Render(&groupLcd, menu, cursor, groupStyle);
            assert(renderedIndex == expected);
        };
        expectCursor(0);
        ScreenDetail_OnRotate(menu, &cursor, -1); expectCursor(SCREEN_LIST_BACK);
        assert(std::string(ScreenDetail_ConfirmLabel(menu, cursor)) == "Back");
        assert(ScreenDetail_OnConfirm(menu, cursor));
        ScreenDetail_OnRotate(menu, &cursor, -128); expectCursor(SCREEN_LIST_BACK);
        ScreenDetail_OnRotate(menu, &cursor, 1); expectCursor(0);
        for (uint8_t item = 1; item <= last; ++item) {
            ScreenDetail_OnRotate(menu, &cursor, 1); expectCursor(item);
        }
        ScreenDetail_OnRotate(menu, &cursor, 1); expectCursor(last);
        ScreenDetail_OnRotate(menu, &cursor, 127); expectCursor(last);
        ScreenDetail_OnRotate(menu, &cursor, 0); expectCursor(last);
        ScreenDetail_OnRotate(menu, &cursor, -1); expectCursor(last - 1);
        ScreenDetail_OnRotate(menu, &cursor, -128); expectCursor(SCREEN_LIST_BACK);
        ScreenDetail_OnRotate(menu, &cursor, 1); expectCursor(0);
        ScreenDetail_OnRotate(menu, &cursor, 127); expectCursor(last);
        assert(ScreenDetail_OnBack(menu));
    }
    // Charge entry has its own confirmation and never invokes discharge start.
    {
        ScreenUiStyle style={}; ST7789_Handle lcd={};
        auto cursor=ScreenDetail_InitIndex(ScreenConfig::kPower);
        ScreenDetail_OnRotate(ScreenConfig::kPower,&cursor,3);
        assert(cursor==3 && !ScreenDetail_OnConfirm(ScreenConfig::kPower,cursor));
        ScreenDetail_Render(&lcd,ScreenConfig::kPower,cursor,style);
        assert(renderedTitle=="Charge Test");
        assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower,cursor));
        ScreenDetail_Render(&lcd,ScreenConfig::kPower,cursor,style);
        assert(renderedTitle=="Start Charge Test" && chargeStarts==0);
        assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower,cursor));
        assert(chargeStarts==1 && batteryStarts==1 && saves==0);
        assert(!ScreenDetail_OnBack(ScreenConfig::kPower));
        assert(!ScreenDetail_OnBack(ScreenConfig::kPower));
        assert(ScreenDetail_OnBack(ScreenConfig::kPower));
    }
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
    // Actions must resolve through the parent group to the currently displayed child.
    index = ScreenDetail_InitIndex(ScreenConfig::kLed);
    assert(std::string(ScreenDetail_ConfirmLabel(ScreenConfig::kLed, index)) == "Open");
    assert(!ScreenDetail_OnConfirm(ScreenConfig::kLed, index));
    auto& leds = STORAGE_MANAGER.config.profiles[0].ledsConfigs;
    leds.ledEnabled = true;
    assert(std::string(ScreenDetail_ConfirmLabel(ScreenConfig::kLed, index)) == "Apply");
    leds.ledEnabled = false;
    assert(std::string(ScreenDetail_ConfirmLabel(ScreenConfig::kLed, index)) == "Apply");
    assert(!ScreenDetail_OnBack(ScreenConfig::kLed));
    ScreenDetail_OnRotate(ScreenConfig::kLed, &index, 2);
    assert(!ScreenDetail_OnConfirm(ScreenConfig::kLed, index));
    leds.aroundLedEnabled = true;
    assert(std::string(ScreenDetail_ConfirmLabel(ScreenConfig::kLed, index)) == "Apply");
    leds.aroundLedEnabled = false;
    assert(std::string(ScreenDetail_ConfirmLabel(ScreenConfig::kLed, index)) == "Apply");
    index = ScreenDetail_InitIndex(ScreenConfig::kPower);
    assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower, index));
    ScreenConfig::setEnabled(STORAGE_MANAGER.config.screenControl, true);
    assert(std::string(ScreenDetail_ConfirmLabel(ScreenConfig::kPower, index)) == "OFF");
    assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower, index));
    assert(std::string(ScreenDetail_ConfirmLabel(ScreenConfig::kPower, index)) == "ON");
    assert(!ScreenDetail_OnBack(ScreenConfig::kPower));
    ScreenDetail_OnRotate(ScreenConfig::kPower, &index, 1);
    assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower, index));
    assert(ScreenDetail_Kind(ScreenConfig::kPower) == (HBOX_AUTO_SLEEP_ENABLED ? SCREEN_DETAIL_SLIDER : SCREEN_DETAIL_INFO));
    STORAGE_MANAGER.config.power.autoSleepEnabled = 1;
    assert(std::string(ScreenDetail_ConfirmLabel(ScreenConfig::kPower, index)) == (HBOX_AUTO_SLEEP_ENABLED ? "OFF" : "N/A"));
    STORAGE_MANAGER.config.power.autoSleepEnabled = 0;
    assert(std::string(ScreenDetail_ConfirmLabel(ScreenConfig::kPower, index)) == (HBOX_AUTO_SLEEP_ENABLED ? "ON" : "N/A"));
    for (uint8_t menu : {0, 1, 2, 5, 7, 15}) {
        assert(ScreenDetail_OnConfirm(menu, SCREEN_LIST_BACK));
        assert(std::string(ScreenDetail_ConfirmLabel(menu, SCREEN_LIST_BACK)) == "Back");
    }

    // Confirming any brightness retains value/enabled state, queues a save and returns.
    for (uint8_t child : {0, 2}) {
        for (bool enabled : {false, true}) {
            auto& on = child == 0 ? leds.ledEnabled : leds.aroundLedEnabled;
            auto& value = child == 0 ? leds.ledBrightness : leds.aroundLedBrightness;
            on = enabled; value = 60;
            index = ScreenDetail_InitIndex(ScreenConfig::kLed);
            ScreenDetail_OnRotate(ScreenConfig::kLed, &index, child);
            assert(!ScreenDetail_OnConfirm(ScreenConfig::kLed, index));
            assert(ScreenUI_ActionIcon(ScreenDetail_ConfirmLabel(ScreenConfig::kLed, index)) == ScreenActionIcon::Confirm);
            const auto saved = saves;
            assert(!ScreenDetail_OnConfirm(ScreenConfig::kLed, index));
            assert(on == enabled && value == 60 && saves == saved + 1);
            assert(ScreenDetail_Kind(ScreenConfig::kLed) == SCREEN_DETAIL_LIST);
            ScreenDetail_Render(&lcd, ScreenConfig::kLed, index, style);
            assert(renderedTitle == "LED Setting" && renderedIndex == child);
            // Re-enter, rotate to apply a new value, then confirm back to the same row.
            assert(!ScreenDetail_OnConfirm(ScreenConfig::kLed, index));
            ScreenDetail_OnRotate(ScreenConfig::kLed, &index, 1);
            assert(value == 70 && on);
            assert(!ScreenDetail_OnConfirm(ScreenConfig::kLed, index));
            assert(value == 70 && on && ScreenDetail_Kind(ScreenConfig::kLed) == SCREEN_DETAIL_LIST);
        }
    }
    for (uint8_t value : {0, 60, 100}) {
        STORAGE_MANAGER.config.screenControl.brightness = value;
        index = ScreenDetail_InitIndex(8);
        assert(ScreenUI_ActionIcon(ScreenDetail_ConfirmLabel(8, index)) == ScreenActionIcon::Confirm);
        const auto saved = saves;
        assert(ScreenDetail_OnConfirm(8, index));
        assert(STORAGE_MANAGER.config.screenControl.brightness == value && saves == saved + 1);
    }
    for (uint8_t menu : {4, 6}) {
        auto cursor = ScreenDetail_InitIndex(menu);
        assert(ScreenDetail_OnConfirm(menu, cursor)); // Direct entry also exits.
    }

    // Preset -> custom -> edit -> done -> Back restores the Custom preset cursor.
    auto& profile = STORAGE_MANAGER.config.profiles[0];
    index = ScreenDetail_InitIndex(11);
    ScreenDetail_OnRotate(11, &index, 127); assert(index == 3);
    assert(std::string(ScreenDetail_ConfirmLabel(11, index)) == "Open");
    assert(!ScreenDetail_OnConfirm(11, index));
    ScreenDetail_Render(&lcd, 11, index, style);
    assert(std::string(ScreenDetail_ConfirmLabel(11, index)) == "Edit");
    assert(!ScreenDetail_OnConfirm(11, index));
    assert(ScreenDetail_Kind(11) == SCREEN_DETAIL_SLIDER);
    assert(std::string(ScreenDetail_ConfirmLabel(11, index)) == "Done");
    assert(!ScreenDetail_OnConfirm(11, index));
    assert(ScreenDetail_Kind(11) == SCREEN_DETAIL_LIST);
    ScreenDetail_OnRotate(11, &index, -128);
    ScreenDetail_Render(&lcd, 11, index, style);
    assert(std::string(ScreenDetail_ConfirmLabel(11, index)) == "Back");
    assert(!ScreenDetail_OnConfirm(11, index));
    ScreenDetail_Render(&lcd, 11, index, style); assert(renderedIndex == 3);
    assert(std::string(ScreenDetail_ConfirmLabel(11, index)) == "Open");
    // Custom list boundaries include Back and clear the previous frame's arrows.
    assert(!ScreenDetail_OnConfirm(11, index));
    ScreenDetail_Render(&lcd, 11, index, style); assert(arrowUp && arrowDown);
    ScreenDetail_OnRotate(11, &index, -128);
    ScreenDetail_Render(&lcd, 11, index, style); assert(!arrowUp && arrowDown);
    ScreenDetail_OnRotate(11, &index, 127);
    ScreenDetail_Render(&lcd, 11, index, style); assert(arrowUp && !arrowDown);
    assert(!ScreenDetail_OnBack(11));
    // Re-entry and hold-to-return use the same internal cursor.
    assert(!ScreenDetail_OnConfirm(11, index));
    assert(!ScreenDetail_OnBack(11));
    ScreenDetail_OnRotate(11, &index, -128); assert(index == SCREEN_LIST_BACK);
    assert(ScreenDetail_OnConfirm(11, index));

    // Disabled profile slots are never applied, and an empty list still offers Back.
    STORAGE_MANAGER.config.numProfilesMax = 3;
    for (auto& p : STORAGE_MANAGER.config.profiles) p.enabled = false;
    index = ScreenDetail_InitIndex(1); assert(index == SCREEN_LIST_BACK);
    assert(ScreenDetail_OnConfirm(1, index));
    STORAGE_MANAGER.config.profiles[2].enabled = true;
    strcpy(STORAGE_MANAGER.config.profiles[2].id, "third");
    ScreenDetail_OnRotate(1, &index, 1); assert(index == 0);
    assert(ScreenDetail_OnConfirm(1, index));
    assert(std::string(STORAGE_MANAGER.config.defaultProfileId) == "third");

    // A blocked SOCD action becomes a dismissible popup, not a scrollable list.
    profile.isCompetitionProfile = true;
    index = ScreenDetail_InitIndex(2);
    assert(!ScreenDetail_OnConfirm(2, 1));
    assert(ScreenDetail_Kind(2) == SCREEN_DETAIL_INFO);
    assert(std::string(ScreenDetail_ConfirmLabel(2, index)) == "Close");
    auto before = index; ScreenDetail_OnRotate(2, &index, -1); assert(index == before);
    assert(!ScreenDetail_OnConfirm(2, index));
    assert(ScreenDetail_Kind(2) == SCREEN_DETAIL_LIST);
    assert(!ScreenDetail_OnConfirm(2, 1));
    tick += 5000;
    assert(ScreenDetail_Kind(2) == SCREEN_DETAIL_LIST);
    ScreenDetail_OnRotate(2, &index, -128); assert(index == SCREEN_LIST_BACK);
    const auto socd = profile.keysConfig.socdMode;
    assert(ScreenDetail_OnConfirm(2, index)); assert(profile.keysConfig.socdMode == socd);
    puts("screen config migration, JSON atomicity and grouped navigation passed");
}
