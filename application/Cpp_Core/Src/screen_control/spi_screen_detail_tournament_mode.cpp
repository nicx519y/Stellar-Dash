#include "screen_control/spi_screen_detail_entries.hpp"

#include "screen_control/spi_screen_detail_render_helpers.hpp"

uint8_t ScreenDetailTournament_InitIndex(void) {
    return 0;
}

void ScreenDetailTournament_Rotate(uint8_t* ioIndex, int8_t det) {
    (void)ioIndex;
    (void)det;
}

void ScreenDetailTournament_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    (void)index;
    ScreenDetailRender_Info(lcd, "Tournament Mode", "Tournament Mode", style);
}

void ScreenDetailTournament_OnConfirm(uint8_t index) {
    (void)index;
}
