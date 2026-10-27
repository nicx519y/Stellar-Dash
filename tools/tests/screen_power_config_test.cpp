#include "screen_control_json.hpp"
#include "screen_control/screen_deferred_save.hpp"
#include "screen_control/spi_screen_detail_pages.hpp"
#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control/spi_screen_ui_common.hpp"
#include "storagemanager.hpp"
#include "board_mode.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "leds/lighting_resources.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

StorageStub STORAGE_MANAGER;
BoardStub BOARD_MODE;
unsigned saves = 0;
unsigned resets = 0;
extern "C" void MainRuntime_RequestReset(void) { ++resets; }
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
std::vector<std::string> renderedLabels;
uint8_t renderedIndex, renderedMax;
void ScreenUI_RequestDeferredSave(uint32_t delay) { assert(delay == 2000); ++saves; }
void ScreenDetailRender_List(ST7789_Handle*, const char* title, const char* const* labels, uint8_t count, uint8_t index, uint8_t, const ScreenUiStyle&, uint16_t, bool, int, uint32_t, uint32_t, const bool*, bool) {
    renderedTitle = title; renderedIndex = index;
    renderedLabels.assign(labels,labels+count);
}
void ScreenDetailRender_Slider(ST7789_Handle*, const char* title, uint8_t index, const ScreenUiStyle&, uint8_t max, const char* value) {
    assert(max == 5 || max == 100); renderedTitle = title; renderedValue = value ? value : ""; renderedIndex = index; renderedMax = max;
}
void ScreenDetailRender_TitleLines(ST7789_Handle*, const char* title, const char* const* lines, uint8_t count, const ScreenUiStyle&) {
    renderedTitle=title; renderedValue=count?lines[0]:"";
}
static unsigned batteryStarts=0,batteryEnds=0,chargeStarts=0,batteryClears=0;
static bool batteryActive=false;
extern "C" int BatteryTest_Active() { return batteryActive; }
extern "C" void BatteryTest_RequestClear() { ++batteryClears; }
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
        ScreenDetail_OnRotate(ScreenConfig::kPower,&cursor,3);
        assert(!ScreenDetail_OnConfirm(ScreenConfig::kPower,cursor)); // end
        assert(batteryEnds==1 && saves==0);
        assert(!ScreenDetail_OnBack(ScreenConfig::kPower)); // status -> list
        assert(!ScreenDetail_OnBack(ScreenConfig::kPower)); // group
    }
    // Both menus share the journal. Navigation/cancel never requests an erase.
    for(uint8_t menu : {18,19}) {
        ScreenUiStyle style={}; ST7789_Handle lcd={};
        auto cursor=ScreenDetail_InitIndex(menu);
        ScreenDetail_Render(&lcd,menu,cursor,style);
        assert((renderedLabels==std::vector<std::string>{"Start Test","View Status","Clear Records","End Test"}));
        ScreenDetail_OnRotate(menu,&cursor,2); assert(cursor==2);
        const auto clears=batteryClears;
        assert(!ScreenDetail_OnConfirm(menu,cursor));
        assert(std::string(ScreenDetail_ConfirmLabel(menu,cursor))=="Clear");
        assert(ScreenUI_ActionIcon(ScreenDetail_ConfirmLabel(menu,cursor))==ScreenActionIcon::Confirm);
        ScreenDetail_Render(&lcd,menu,cursor,style);
        assert(renderedTitle=="Clear Records" && renderedValue=="Delete all test records?");
        assert(!ScreenDetail_OnBack(menu) && batteryClears==clears);
        assert(!ScreenDetail_OnConfirm(menu,cursor));
        assert(!ScreenDetail_OnConfirm(menu,cursor) && batteryClears==clears+1);
        assert(!ScreenDetail_OnConfirm(menu,cursor)); // result -> list
        batteryActive=true;
        assert(!ScreenDetail_OnConfirm(menu,cursor));
        assert(std::string(ScreenDetail_ConfirmLabel(menu,cursor))=="Back");
        ScreenDetail_Render(&lcd,menu,cursor,style);
        assert(renderedValue=="End test before clearing");
        assert(!ScreenDetail_OnConfirm(menu,cursor) && batteryClears==clears+1);
        batteryActive=false;
        assert(!ScreenDetail_OnConfirm(menu,cursor)); // confirmation
        batteryActive=true; // another entry started a test meanwhile
        assert(!ScreenDetail_OnConfirm(menu,cursor) && batteryClears==clears+1);
        batteryActive=false;
        assert(!ScreenDetail_OnBack(menu));
        ScreenDetail_OnRotate(menu,&cursor,1); assert(cursor==3);
        const auto ends=batteryEnds;
        assert(!ScreenDetail_OnConfirm(menu,cursor) && batteryEnds==ends+1);
        assert(!ScreenDetail_OnBack(menu));
        ScreenDetail_OnRotate(menu,&cursor,-128); assert(cursor==SCREEN_LIST_BACK);
        assert(ScreenDetail_OnConfirm(menu,cursor));
        assert(saves==0);
    }
    auto defaults = legacy();
    // Old records gain the default; the marker/RGB survive normalization, raw persistence and JSON.
    ScreenConfig::normalize(defaults);
    assert(ScreenConfig::highlightColor(defaults) == ScreenConfig::kDefaultHighlight);
    for (uint32_t color : {0u, 0xFFFFFFu, 0x52A8FFu}) {
        ScreenConfig::setHighlightColor(defaults, color);
        ScreenConfig::normalize(defaults);
        ScreenControlConfig restored;
        memcpy(&restored, &defaults, sizeof(restored));
        assert(ScreenConfig::highlightColor(restored) == color && sizeof(restored) == 68);
        auto* json = ScreenConfig::toJson(restored);
        ScreenControlConfig imported{};
        assert(!ScreenConfig::applyJson(imported, json, true)); cJSON_Delete(json);
        assert(ScreenConfig::highlightColor(imported) == color);
        assert(!patch(imported, R"({"brightness":42})"));
        assert(ScreenConfig::highlightColor(imported) == color);
        for (const char* bad : {R"({"highlightColor":-1})", R"({"highlightColor":16777216})",
                                R"({"highlightColor":1.5})", R"({"highlightColor":"blue"})", R"({"highlightColor":null})"}) {
            const auto before = imported;
            assert(patch(imported, bad));
            assert(!memcmp(&before, &imported, sizeof(before)));
        }
        assert(!patch(imported, R"({"brightness":42})", true));
        assert(ScreenConfig::highlightColor(imported) == ScreenConfig::kDefaultHighlight);
    }
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
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 12);
    for (unsigned i = 0; i < 12; ++i) assert(ScreenConfig::isTopLevel(menuIds[i]));
    assert(menuIds[11] == ScreenConfig::kLanguage);
    assert(ScreenMain_FindMenuMeta(12)); // Maintenance remains restorable but never a normal menu.
    menuConfig.featuresMask &= ~(1u << 14);
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 11);
    BOARD_MODE.allowed = false;
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 10);
    BOARD_MODE.allowed = true;
    menuConfig.featuresMask = 0;
    assert(ScreenMain_RebuildMenuIds(menuConfig, menuIds, 12) == 2);
    assert(menuIds[0] == ScreenConfig::kConnection && menuIds[1] == ScreenConfig::kLanguage);
    menuConfig.currentPageId = ScreenConfig::kLanguage;
    ScreenConfig::normalize(menuConfig);
    assert(menuConfig.currentPageId == ScreenConfig::kLanguage);
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
    // Real dispatcher: OFF + five durations; confirm returns without toggling.
    STORAGE_MANAGER.config.screenControl = sc;
    init_power_defaults(STORAGE_MANAGER.config.power);
    ScreenUiStyle style = {}; ST7789_Handle lcd = {};
    uint8_t index;
    for (uint8_t child : {0, 1}) {
        auto& screen = STORAGE_MANAGER.config.screenControl;
        auto& power = STORAGE_MANAGER.config.power;
        ScreenConfig::setEnabled(screen, false); screen.standbyTimeoutSeconds = 120;
        power.autoSleepEnabled = 0; power.autoStandbyMs = 300000;
        const auto displayMode = screen.standbyDisplay;
        const auto seconds = [&]() { return child ? power.autoStandbyMs / 1000u : screen.standbyTimeoutSeconds; };
        const auto enabled = [&]() { return child ? power.autoSleepEnabled == 1 : ScreenConfig::enabled(screen); };
        const auto render = [&]() { ScreenDetail_Render(&lcd, 14, index, style); };
        index = ScreenDetail_InitIndex(14);
        ScreenDetail_OnRotate(14, &index, child);
        assert(!ScreenDetail_OnConfirm(14, index));
        if (child && !HBOX_AUTO_SLEEP_ENABLED) {
            const auto saved = saves;
            ScreenDetail_OnRotate(14, &index, 127);
            assert(!ScreenDetail_OnConfirm(14, index));
            render(); assert(renderedValue == "Unavailable");
            assert(std::string(ScreenDetail_ConfirmLabel(14, index)) == "N/A");
            assert(!enabled() && seconds() == 300 && saves == saved);
            assert(!ScreenDetail_OnBack(14));
            continue;
        }
        const char* labels[] = {"OFF", "10s", "30s", "1min", "2min", "5min"};
        const unsigned times[] = {0, 10, 30, 60, 120, 300};
        for (unsigned step = 0; step <= 5; ++step) {
            if (step) ScreenDetail_OnRotate(14, &index, 1);
            render();
            assert(renderedTitle == (child ? "Auto Sleep" : "Screen Standby"));
            assert(renderedIndex == step && renderedMax == 5 && renderedValue == labels[step]);
            assert(enabled() == (step != 0));
            if (step) assert(seconds() == times[step]);
            assert(ScreenUI_ActionIcon(ScreenDetail_ConfirmLabel(14, index)) == ScreenActionIcon::Confirm);
        }
        auto saved = saves;
        ScreenDetail_OnRotate(14, &index, 127); render();
        assert(renderedIndex == 5 && saves == saved);
        ScreenDetail_OnRotate(14, &index, -128); render();
        assert(renderedIndex == 0 && !enabled() && seconds() == 300);
        saved = saves;
        ScreenDetail_OnRotate(14, &index, -128); assert(saves == saved);
        assert(!ScreenDetail_OnConfirm(14, index));
        assert(!enabled() && seconds() == 300 && saves == saved);
        render(); assert(renderedTitle == "Power" && renderedIndex == child);
        // Re-entry preserves OFF; selecting a duration enables immediately.
        assert(!ScreenDetail_OnConfirm(14, index)); render(); assert(renderedValue == "OFF");
        ScreenDetail_OnRotate(14, &index, 1);
        assert(enabled() && seconds() == 10);
        assert(!ScreenDetail_OnConfirm(14, index)); render();
        assert(enabled() && seconds() == 10 && renderedTitle == "Power");
        // WebConfig changes are reflected without reopening the slider.
        assert(!ScreenDetail_OnConfirm(14, index));
        if (child) power.autoStandbyMs = 120000;
        else screen.standbyTimeoutSeconds = 120;
        render(); assert(renderedIndex == 4 && renderedValue == "2min");
        if (child) power.autoSleepEnabled = 0;
        else ScreenConfig::setEnabled(screen, false);
        render(); assert(renderedIndex == 0 && renderedValue == "OFF");
        assert(screen.standbyDisplay == displayMode);
        assert(!ScreenDetail_OnBack(14));
        render(); assert(renderedTitle == "Power" && renderedIndex == child);
        assert(ScreenDetail_OnBack(14));
    }
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
    for (uint8_t menu : {0, 1, 2, 5, 7, 15}) {
        assert(ScreenDetail_OnConfirm(menu, SCREEN_LIST_BACK));
        assert(std::string(ScreenDetail_ConfirmLabel(menu, SCREEN_LIST_BACK)) == "Back");
    }

    // Confirming any brightness only returns; rotation owns the pending save.
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
            assert(on == enabled && value == 60 && saves == saved);
            assert(ScreenDetail_Kind(ScreenConfig::kLed) == SCREEN_DETAIL_LIST);
            ScreenDetail_Render(&lcd, ScreenConfig::kLed, index, style);
            assert(renderedTitle == "LED Setting" && renderedIndex == child);
            // Re-enter, rotate to apply a new value, then confirm back to the same row.
            assert(!ScreenDetail_OnConfirm(ScreenConfig::kLed, index));
            ScreenDetail_OnRotate(ScreenConfig::kLed, &index, 1);
            assert(value == 70 && on);
            assert(!ScreenDetail_OnConfirm(ScreenConfig::kLed, index));
            assert(value == 70 && on && saves == saved + 1 && ScreenDetail_Kind(ScreenConfig::kLed) == SCREEN_DETAIL_LIST);
        }
    }
    for (uint8_t value : {0, 60, 100}) {
        STORAGE_MANAGER.config.screenControl.brightness = value;
        index = ScreenDetail_InitIndex(8);
        assert(ScreenUI_ActionIcon(ScreenDetail_ConfirmLabel(8, index)) == ScreenActionIcon::Confirm);
        const auto saved = saves;
        assert(ScreenDetail_OnConfirm(8, index));
        assert(STORAGE_MANAGER.config.screenControl.brightness == value && saves == saved);
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
    // Language uses a reserved byte; migration and JSON preserve all other settings.
    for (uint8_t schema : {0u, 0xA1u, 0xA2u}) {
        auto old = legacy(2);
        old.reservedStyle[0]=schema; old.reservedStyle[1]=1; old.reservedStyle[2]=1;
        ScreenConfig::normalize(old);
        assert(ScreenConfig::language(old)==0 && ScreenConfig::enabled(old));
        assert(old.standbyTimeoutSeconds==120 && old.standbyDisplay==2);
    }
    auto& localized=STORAGE_MANAGER.config.screenControl;
    ScreenConfig::normalize(localized);
    index=ScreenDetail_InitIndex(ScreenConfig::kLanguage); assert(index==0);
    ScreenDetail_OnRotate(ScreenConfig::kLanguage,&index,1); assert(index==1);
    const auto savesBeforeLanguage=saves;
    assert(ScreenDetail_OnConfirm(ScreenConfig::kLanguage,index));
    assert(ScreenConfig::language(localized)==1 && ScreenText_GetLanguage()==1 && saves==savesBeforeLanguage+1);
    auto persisted=localized;
    ScreenConfig::normalize(persisted); assert(ScreenConfig::language(persisted)==1);
    auto* json=ScreenConfig::toJson(persisted);
    ScreenControlConfig imported={};
    assert(!ScreenConfig::applyJson(imported,json,true)); cJSON_Delete(json);
    assert(ScreenConfig::language(imported)==1);
    assert(!patch(imported,R"({"brightness":40})") && ScreenConfig::language(imported)==1);
    auto beforeBadLanguage=imported;
    assert(patch(imported,R"({"language":"bad","brightness":30})"));
    assert(!memcmp(&imported,&beforeBadLanguage,sizeof(imported)));
    assert(!patch(imported,R"({"brightness":40})",true) && ScreenConfig::language(imported)==0);
    ScreenDetail_OnRotate(ScreenConfig::kLanguage,&index,-128); assert(index==SCREEN_LIST_BACK);
    assert(ScreenDetail_OnConfirm(ScreenConfig::kLanguage,index) && ScreenText_GetLanguage()==1);
    index=0; assert(ScreenDetail_OnConfirm(ScreenConfig::kLanguage,index));
    assert(ScreenText_GetLanguage()==0 && ScreenConfig::language(localized)==0);
    // Current language confirmation is navigation only, including a second click after applying.
    auto saved = saves;
    assert(ScreenDetail_OnConfirm(ScreenConfig::kLanguage, 0));
    assert(saves == saved);

    // Platform rows use their displayed identity: legacy PS4 and PS5 share the PS row.
    const InputMode modes[] = {INPUT_MODE_XINPUT, INPUT_MODE_PS5, INPUT_MODE_XBOX, INPUT_MODE_SWITCH};
    for (uint8_t i=0; i<4; ++i) {
        STORAGE_MANAGER.config.inputMode=modes[i];
        const auto writes=STORAGE_MANAGER.inputWrites, commits=STORAGE_MANAGER.commits;
        const auto boots=STORAGE_MANAGER.bootWrites, resetCount=resets;
        assert(ScreenDetail_OnConfirm(0,i));
        assert(STORAGE_MANAGER.inputWrites==writes && STORAGE_MANAGER.commits==commits);
        assert(STORAGE_MANAGER.bootWrites==boots && resets==resetCount);
        assert(ScreenDetail_OnConfirm(0,(i+1)%4));
        assert(STORAGE_MANAGER.config.inputMode==modes[(i+1)%4]);
        assert(STORAGE_MANAGER.inputWrites==writes+1 && STORAGE_MANAGER.commits==commits+1 && resets==resetCount+1);
    }
    STORAGE_MANAGER.config.inputMode=INPUT_MODE_PS4;
    const auto resetCount=resets;
    assert(ScreenDetail_OnConfirm(0,1) && resets==resetCount && STORAGE_MANAGER.config.inputMode==INPUT_MODE_PS4);

    // Sparse profile slots compare IDs, not the visible row number.
    index=ScreenDetail_InitIndex(1);
    const auto profileWrites=STORAGE_MANAGER.profileWrites;
    assert(ScreenDetail_OnConfirm(1,index));
    assert(STORAGE_MANAGER.profileWrites==profileWrites);

    // Same effects must never restart their runtime; a different effect still applies once.
    for (uint8_t menu : {5,7}) {
        leds.aroundLedSyncToMainLed=false;
        index=ScreenDetail_InitIndex(menu);
        const auto selections=LightingResources::selections;
        saved=saves;
        assert(ScreenDetail_OnConfirm(menu,index));
        assert(LightingResources::selections==selections && saves==saved);
        index=menu==5 ? (index==0 ? 1 : 0) : (index==1 ? 2 : 1);
        assert(ScreenDetail_OnConfirm(menu,index));
        assert(LightingResources::selections==selections+1 && saves==saved+1);
        assert(ScreenDetail_OnConfirm(menu,index));
        assert(LightingResources::selections==selections+1 && saves==saved+1);
    }
    leds.aroundLedSyncToMainLed=true;
    saved=saves;
    assert(ScreenDetail_OnConfirm(7,0) && saves==saved);
    // Leaving Follow for the already stored effect is a real change.
    const auto selections=LightingResources::selections;
    assert(ScreenDetail_OnConfirm(7,2) && !leds.aroundLedSyncToMainLed);
    assert(saves==saved+1 && LightingResources::selections==selections+1);

    // Each preset applies once; repeated confirmation preserves every per-key field.
    for (uint8_t preset : {0,1,2}) {
        index=ScreenDetail_InitIndex(11);
        ScreenDetail_OnRotate(11,&index,-128);
        ScreenDetail_OnRotate(11,&index,preset+1);
        const auto setups=ADC_BTNS_WORKER.setups;
        saved=saves;
        assert(ScreenDetail_OnConfirm(11,index));
        assert(ADC_BTNS_WORKER.setups==setups+1 && saves==saved+1);
        const auto applied=profile;
        index=ScreenDetail_InitIndex(11); assert(index==preset);
        assert(ScreenDetail_OnConfirm(11,index));
        assert(ADC_BTNS_WORKER.setups==setups+1 && saves==saved+1);
        assert(!memcmp(&applied,&profile,sizeof(profile)));
    }
    // Custom is an editor entry, even when currently active. Done does not apply twice.
    index=ScreenDetail_InitIndex(11); ScreenDetail_OnRotate(11,&index,127);
    assert(!ScreenDetail_OnConfirm(11,index));
    assert(!ScreenDetail_OnConfirm(11,index));
    saved=saves;
    const auto setups=ADC_BTNS_WORKER.setups;
    ScreenDetail_OnRotate(11,&index,1);
    assert(saves==saved+1 && ADC_BTNS_WORKER.setups==setups+1);
    assert(!ScreenDetail_OnConfirm(11,index));
    assert(saves==saved+1 && ADC_BTNS_WORKER.setups==setups+1);
    assert(!ScreenDetail_OnBack(11));
    index=ScreenDetail_InitIndex(11); assert(index==3);
    assert(!ScreenDetail_OnConfirm(11,index));
    assert(std::string(ScreenDetail_ConfirmLabel(11,index))=="Edit");
    puts("screen config, active-option no-op, deferred saves and grouped navigation passed");
}
