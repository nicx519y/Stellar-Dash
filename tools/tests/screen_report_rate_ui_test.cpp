#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>
#include "board_mode.hpp"
#include "connection_manager.hpp"
#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

StorageStub STORAGE_MANAGER;
BoardStub BOARD_MODE;
ConnectionStub CONNECTION_MANAGER;

struct Rect { uint16_t x, y, width, height; uint32_t color; };
static std::vector<Rect> rectangles;
static std::vector<std::string> labels;

void ST7789_FillRect(ST7789_Handle*, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    assert(x + w <= ST7789_WIDTH && y + h <= ST7789_HEIGHT);
    rectangles.push_back({x, y, w, h, color});
}
void ST7789_DrawString(ST7789_Handle*, uint16_t x, uint16_t y, const char* text, uint32_t, uint32_t, uint8_t scale) {
    assert(x + std::string(text).size() * 6u * scale <= ST7789_WIDTH);
    assert(y + 8u * scale <= ST7789_HEIGHT);
    labels.emplace_back(text);
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
}
