#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>
#include "screen_control/spi_screen_ui_common.hpp"
#include "board_mode.hpp"
#include "connection_manager.hpp"
#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

StorageStub STORAGE_MANAGER;
BoardStub BOARD_MODE;
ConnectionStub CONNECTION_MANAGER;

struct Rect { uint16_t x, y, width, height; uint32_t color; };
struct TextInk { std::string label; uint32_t fg, bg; };
static std::vector<TextInk> textInk;
static std::vector<Rect> rectangles;
static std::vector<std::string> labels;
static uint32_t pixels[ST7789_HEIGHT][ST7789_WIDTH] = {};
static bool arrowVisible(bool up) {
    const unsigned start = up ? 0 : (ST7789_HEIGHT / 3u) * 2u;
    const unsigned end = up ? ST7789_HEIGHT / 3u : ST7789_HEIGHT;
    for (unsigned y = start; y < end; ++y)
        for (unsigned x = ST7789_WIDTH - SPI_SCREEN_RIGHT_BAR_W; x < ST7789_WIDTH; ++x)
            if (pixels[y][x] == 0xffffff) return true;
    return false;
}

void ST7789_FillRect(ST7789_Handle*, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    assert(x + w <= ST7789_WIDTH && y + h <= ST7789_HEIGHT);
    rectangles.push_back({x, y, w, h, color});
    for (unsigned yy = y; yy < y + h; ++yy)
        for (unsigned xx = x; xx < x + w; ++xx) pixels[yy][xx] = color;
}
void ST7789_DrawString(ST7789_Handle*, uint16_t x, uint16_t y, const char* text, uint32_t fg, uint32_t bg, uint8_t scale) {
    assert(x + ScreenUI_TextPxW(text, scale) <= ST7789_WIDTH);
    assert(y + ScreenUI_CharCellH(scale) <= ST7789_HEIGHT);
    labels.emplace_back(text);
    textInk.push_back({text, fg, bg});
}
void ScreenUI_RequestDeferredSave(uint32_t) { std::abort(); }

int main() {
    ST7789_Handle lcd{};
    const ScreenUiStyle style{0, 0xffffff, 0x333333, 0};
    const WirelessReportRate rates[] = {RFM_RATE_1K, RFM_RATE_2K, RFM_RATE_4K, RFM_RATE_8K};
    const char* names[] = {"1K", "2K", "4K", "8K"};
    for (uint8_t i = 0; i < 4; ++i) {
        STORAGE_MANAGER.rate = rates[i];
        assert(ScreenDetailTournament_InitIndex() == i);
        rectangles.clear(); labels.clear();
        ScreenDetailTournament_Render(&lcd, i, style);
        assert(labels.size() == 2 && labels[0] == "Report Rate" && labels[1] == names[i]);
        assert(rectangles.size() == 3);
        assert(rectangles[2].width == rectangles[1].width * i / 3u);
        uint8_t index = i;
        ScreenDetailTournament_Rotate(&index, 1);
        assert(index == (i < 3 ? i + 1 : 3));
        index = i;
        ScreenDetailTournament_Rotate(&index, -1);
        assert(index == (i > 0 ? i - 1 : 0));
        assert(STORAGE_MANAGER.rate == rates[i]); // Selection waits for confirmation.
        // Same-rate confirmation must exit before any board/RF/save call (stubs abort).
        assert(ScreenDetailTournament_OnConfirm(i));
    }
    uint8_t index = 0;
    ScreenDetailTournament_Rotate(&index, 127); assert(index == 3);
    ScreenDetailTournament_Rotate(&index, -128); assert(index == 0);
    ScreenDetailTournament_Rotate(nullptr, 1);
    assert(ScreenDetailTournament_OnBack());
    rectangles.clear(); labels.clear();
    ScreenDetailTournament_Render(&lcd, 255, style);
    assert(labels.back() == "8K" && rectangles[2].width == rectangles[1].width);
    // Existing brightness callers retain their percentage label and geometry.
    for (uint8_t value : {0, 50, 100}) {
        rectangles.clear(); labels.clear();
        ScreenDetailRender_Slider(&lcd, "Light Brightness", value, style);
        assert(labels[1] == std::to_string(value) + "%");
        assert(rectangles[2].width == rectangles[1].width * value / 100u);
    }
    rectangles.clear(); labels.clear();
    ScreenDetailRender_Slider(&lcd, "Report Rate", 1, style, 0, "2K");
    ScreenDetailRender_Slider(nullptr, "Report Rate", 1, style, 3, "2K");
    assert(rectangles.empty() && labels.empty());
    // Back is a visible, selectable virtual row before every ordinary list.
    const char* items[] = {"USB", "2.4G RF"};
    for (uint8_t count : {0, 2}) {
        rectangles.clear(); labels.clear();
        ScreenDetailRender_List(&lcd, "Connection", items, count, SCREEN_LIST_BACK, 0, style);
        assert(!labels.empty() && labels.front() == "Back");
        assert(rectangles.size() > 3); // Includes the vector return icon.
    }
    rectangles.clear(); labels.clear();
    ScreenDetailRender_List(&lcd, "Main", items, 2, 0, 0, style,
        SPI_SCREEN_MENU_ITEM_H, false, 0, 0, 0, nullptr, false);
    assert(labels.size() == 2 && labels.front() == "USB"); // Root has no parent.
    assert(ScreenList_Rotate(0, -1, 2) == SCREEN_LIST_BACK);
    assert(ScreenList_Rotate(SCREEN_LIST_BACK, -128, 2) == SCREEN_LIST_BACK);
    assert(ScreenList_Rotate(SCREEN_LIST_BACK, 1, 2) == 0);
    assert(ScreenList_Rotate(SCREEN_LIST_BACK, 127, 2) == 1);
    assert(ScreenList_Rotate(1, 127, 2) == 1);
    assert(ScreenList_Rotate(0, 127, 0) == SCREEN_LIST_BACK);
    for (bool up : {false, true}) {
        rectangles.clear();
        ScreenUI_DrawScrollArrow(&lcd, 300, 28, up, style.text);
        assert(rectangles.size() == 6);
        assert(rectangles.front().width == 1 && rectangles.back().width == 11);
        assert(up ? rectangles.front().y < rectangles.back().y : rectangles.front().y > rectangles.back().y);
    }

    const auto layout = ScreenUI_StatusBarLayout();
    // 320x172: retain the prior platform center, offset profile/battery by exactly 5px.
    assert(layout.profileY + ScreenUI_CharCellH(SPI_SCREEN_PROFILE_TEXT_SCALE) / 2u == 23);
    assert(layout.batteryY == 141);
    assert(layout.platformY + SPI_SCREEN_STATUS_ICON_H / 2u == 86);
    assert(layout.topArrowY == layout.profileY + ScreenUI_CharCellH(SPI_SCREEN_PROFILE_TEXT_SCALE) / 2u);
    assert(layout.bottomArrowY == layout.batteryY + SPI_SCREEN_BATTERY_BODY_H / 2u);
    assert(layout.topArrowY == 23 && layout.bottomArrowY == 148);
    assert(layout.connectionY + SPI_SCREEN_STATUS_ICON_H / 2u == (86 + 148) / 2u);
    rectangles.clear();
    ScreenUI_DrawListArrows(&lcd, true, true, style.text, style.bg);
    bool upperTip = false, lowerTip = false;
    for (const auto& r : rectangles) {
        if (r.width == 1 && r.height == 1 && r.color == style.text) {
            upperTip |= r.y == layout.topArrowY - 3;
            lowerTip |= r.y == layout.bottomArrowY + 3;
        }
    }
    assert(upperTip && lowerTip);

    // Reuse the framebuffer across frames to catch arrows left behind at an end.
    auto renderList = [&](uint8_t cursor, uint8_t count, bool back, const bool* disabled = nullptr) {
        ScreenDetailRender_List(&lcd, "List", items, count, cursor, 0, style,
            SPI_SCREEN_DETAIL_ITEM_H, false, 0, 0, 0, disabled, back);
    };
    renderList(0, 2, true); assert(arrowVisible(true) && arrowVisible(false));
    renderList(SCREEN_LIST_BACK, 2, true); assert(!arrowVisible(true) && arrowVisible(false));
    renderList(1, 2, true); assert(arrowVisible(true) && !arrowVisible(false));
    renderList(0, 2, false); assert(!arrowVisible(true) && arrowVisible(false));
    renderList(1, 2, false); assert(arrowVisible(true) && !arrowVisible(false));
    renderList(0, 1, false); assert(!arrowVisible(true) && !arrowVisible(false));
    renderList(SCREEN_LIST_BACK, 0, true); assert(!arrowVisible(true) && !arrowVisible(false));
    const bool disabled[] = {false, true};
    renderList(0, 2, true, disabled); assert(arrowVisible(true) && !arrowVisible(false));
    renderList(SCREEN_LIST_BACK, 2, true, disabled); assert(!arrowVisible(true) && arrowVisible(false));

    struct Action { const char* label; ScreenActionIcon icon; };
    const Action actions[] = {
        {"Open", ScreenActionIcon::Open}, {"Back", ScreenActionIcon::Back},
        {"Apply", ScreenActionIcon::Confirm}, {"OK", ScreenActionIcon::Confirm},
        {"Done", ScreenActionIcon::Confirm}, {"Finish", ScreenActionIcon::Confirm},
        {"ON", ScreenActionIcon::TurnOn}, {"OFF", ScreenActionIcon::TurnOff},
        {"Edit", ScreenActionIcon::Edit}, {"Close", ScreenActionIcon::Close},
        {"Cancel", ScreenActionIcon::Close}, {"Quit", ScreenActionIcon::Exit},
        {"Exit", ScreenActionIcon::Exit}, {"Start", ScreenActionIcon::Start},
        {"Retry", ScreenActionIcon::Retry}, {"Wait", ScreenActionIcon::Wait},
        {"N/A", ScreenActionIcon::Unavailable}, {"Check", ScreenActionIcon::Check},
    };
    std::vector<uint32_t> onPixels, offPixels;
    for (const auto& action : actions) {
        assert(ScreenUI_ActionIcon(action.label) == action.icon);
        rectangles.clear(); labels.clear();
        ScreenUI_DrawActionIcon(&lcd, 300, 85, action.icon, style.text, style.bg);
        assert(rectangles.size() > 1 && labels.empty());
        for (const auto& r : rectangles)
            assert(r.x >= 288 && r.x + r.width <= 312 && r.y >= 73 && r.y + r.height <= 97);
        if (action.icon == ScreenActionIcon::TurnOn || action.icon == ScreenActionIcon::TurnOff) {
            auto& result = action.icon == ScreenActionIcon::TurnOn ? onPixels : offPixels;
            for (unsigned y = 73; y < 97; ++y)
                for (unsigned x = 288; x < 312; ++x) result.push_back(pixels[y][x]);
        }
    }
    assert(onPixels != offPixels);
    assert(ScreenUI_ActionIcon(nullptr) == ScreenActionIcon::None);
    assert(ScreenUI_ActionIcon("") == ScreenActionIcon::None);
    assert(ScreenUI_ActionIcon("unknown") == ScreenActionIcon::None);

    const auto steel = ScreenUI_ColdSteelStyle(false);
    assert(steel.bg == 0x101923 && steel.selBg == 0x30495e);
    assert(steel.text == 0xdce6f0 && steel.mutedText == 0xa9b9cc);
    assert(steel.icon == 0x88b4d7 && steel.track == 0x263b4c);
    assert(steel.accent == 0xb5ed68);
    const auto pink = ScreenUI_ColdSteelStyle(false, 0xff88cc);
    assert(pink.bg == steel.bg && pink.icon == steel.icon && pink.selBg == steel.selBg);
    assert(pink.accent == 0xff88cc);
    assert(ScreenUI_ColdSteelStyle(false, 0).accent != steel.bg);
    assert(ScreenUI_ColdSteelStyle(true, 0xffffff).accent != 0xffffff);
    for (bool light : {false, true}) {
        const auto theme = ScreenUI_ColdSteelStyle(light);
        rectangles.clear(); textInk.clear();
        ScreenDetailRender_List(&lcd, "List", items, 2, 1, 0, theme,
            SPI_SCREEN_MENU_ITEM_H, false, 0, 0, 0, nullptr, false);
        assert(textInk[0].fg == theme.text); // Applied value outside the focus stays neutral.
        assert(textInk[1].fg == theme.accent && textInk[1].bg == theme.selBg);
        rectangles.clear(); textInk.clear();
        ScreenDetailRender_Slider(&lcd, "Brightness", 70, theme);
        assert(rectangles[1].color == theme.track && rectangles[2].color == theme.accent);
        assert(rectangles[1].x == 50 && rectangles[1].y == 72 && rectangles[1].height == 28);
        assert(rectangles[2].width == 154);
        assert(textInk[0].fg == theme.text && textInk[1].fg == theme.text);

        // Arrow-to-slider transition erases every old arrow pixel; middle action is untouched.
        ScreenUI_DrawListArrows(&lcd, true, true, theme.icon, theme.bg);
        pixels[85][300] = 0x123456;
        ScreenUI_DrawSliderControls(&lcd, theme.icon, theme.bg);
        for (unsigned y = 0; y < 172; ++y) for (unsigned x = 280; x < 320; ++x) {
            if (y >= 57 && y < 114) continue;
            const bool plus = (x >= 294 && x < 306 && y >= 22 && y < 24) ||
                              (x >= 299 && x < 301 && y >= 17 && y < 29);
            const bool minus = x >= 294 && x < 306 && y >= 147 && y < 149;
            assert(pixels[y][x] == (plus || minus ? theme.icon : theme.bg));
        }
        assert(pixels[85][300] == 0x123456);
        const uint8_t icon[] = {0,0, 255,255, 0x10,0x84};
        ScreenUI_DrawTintedIcon(&lcd, 6, 103, 3, 1, icon, theme.icon, theme.bg);
        assert(pixels[103][6] == theme.bg && pixels[103][7] == theme.icon);
        assert(pixels[103][8] != theme.bg && pixels[103][8] != theme.icon);
    }

}
