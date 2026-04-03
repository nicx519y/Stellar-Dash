#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static const InputMode kInputModes[] = {
    INPUT_MODE_XINPUT,
    INPUT_MODE_PS4,
    INPUT_MODE_XBOX,
    INPUT_MODE_SWITCH,
};

static const char* kInputModeLabels[] = {
    "PC",
    "PS",
    "Xbox",
    "NS",
};

uint8_t ScreenDetailInputMode_InitIndex(void) {
    InputMode mode = STORAGE_MANAGER.getInputMode();
    for (uint8_t i = 0; i < (uint8_t)(sizeof(kInputModes) / sizeof(kInputModes[0])); i++) {
        if (kInputModes[i] == mode) return i;
    }
    return 0;
}

void ScreenDetailInputMode_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t)(sizeof(kInputModes) / sizeof(kInputModes[0]))) idx = (int32_t)(sizeof(kInputModes) / sizeof(kInputModes[0])) - 1;
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailInputMode_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    uint8_t selected = ScreenDetailInputMode_InitIndex();
    ScreenDetailRender_List(lcd, "Input Mode", kInputModeLabels, (uint8_t)(sizeof(kInputModeLabels) / sizeof(kInputModeLabels[0])), index, selected, style);
}

void ScreenDetailInputMode_OnConfirm(uint8_t index) {
    if (index < (uint8_t)(sizeof(kInputModes) / sizeof(kInputModes[0]))) {
        STORAGE_MANAGER.setInputMode(kInputModes[index]);
        STORAGE_MANAGER.saveConfig();
        ScreenUI_RequestRebootTo(0, index);
    }
}
