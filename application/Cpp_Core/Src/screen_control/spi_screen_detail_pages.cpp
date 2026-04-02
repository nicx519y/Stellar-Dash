#include "screen_control/spi_screen_detail_pages.hpp"

#include "screen_control/spi_screen_detail_entries.hpp"

ScreenDetailKind ScreenDetail_Kind(uint8_t menuId) {
    switch (menuId) {
        case 0:
        case 1:
        case 2:
        case 5:
        case 7:
            return SCREEN_DETAIL_LIST;
        case 4:
        case 6:
        case 8:
            return SCREEN_DETAIL_SLIDER;
        case 9:
        case 10:
        case 3:
            return SCREEN_DETAIL_INFO;
        default:
            return SCREEN_DETAIL_NONE;
    }
}

uint8_t ScreenDetail_InitIndex(uint8_t menuId) {
    switch (menuId) {
        case 0: return ScreenDetailInputMode_InitIndex();
        case 1: return ScreenDetailProfiles_InitIndex();
        case 2: return ScreenDetailSocd_InitIndex();
        case 5: return ScreenDetailLightEffect_InitIndex();
        case 7: return ScreenDetailAmbientEffect_InitIndex();
        case 4: return ScreenDetailLightBrightness_InitIndex();
        case 6: return ScreenDetailAmbientBrightness_InitIndex();
        case 8: return ScreenDetailScreenBrightness_InitIndex();
        case 9: return ScreenDetailWebConfig_InitIndex();
        case 10: return ScreenDetailCalibration_InitIndex();
        case 3: return ScreenDetailTournament_InitIndex();
        default: return 0;
    }
}

void ScreenDetail_OnRotate(uint8_t menuId, uint8_t* ioIndex, int8_t det) {
    switch (menuId) {
        case 0: ScreenDetailInputMode_Rotate(ioIndex, det); break;
        case 1: ScreenDetailProfiles_Rotate(ioIndex, det); break;
        case 2: ScreenDetailSocd_Rotate(ioIndex, det); break;
        case 5: ScreenDetailLightEffect_Rotate(ioIndex, det); break;
        case 7: ScreenDetailAmbientEffect_Rotate(ioIndex, det); break;
        case 4: ScreenDetailLightBrightness_Rotate(ioIndex, det); break;
        case 6: ScreenDetailAmbientBrightness_Rotate(ioIndex, det); break;
        case 8: ScreenDetailScreenBrightness_Rotate(ioIndex, det); break;
        case 9: ScreenDetailWebConfig_Rotate(ioIndex, det); break;
        case 10: ScreenDetailCalibration_Rotate(ioIndex, det); break;
        case 3: ScreenDetailTournament_Rotate(ioIndex, det); break;
        default: break;
    }
}

bool ScreenDetail_OnConfirm(uint8_t menuId, uint8_t index) {
    switch (menuId) {
        case 0: ScreenDetailInputMode_OnConfirm(index); return true;
        case 1: ScreenDetailProfiles_OnConfirm(index); return true;
        case 2: ScreenDetailSocd_OnConfirm(index); return true;
        case 5: ScreenDetailLightEffect_OnConfirm(index); return true;
        case 7: ScreenDetailAmbientEffect_OnConfirm(index); return true;
        case 4: ScreenDetailLightBrightness_OnConfirm(index); return true;
        case 6: ScreenDetailAmbientBrightness_OnConfirm(index); return true;
        case 8: ScreenDetailScreenBrightness_OnConfirm(index); return true;
        case 9: ScreenDetailWebConfig_OnConfirm(index); return true;
        case 10: ScreenDetailCalibration_OnConfirm(index); return true;
        case 3: ScreenDetailTournament_OnConfirm(index); return true;
        default: return false;
    }
}

void ScreenDetail_Render(ST7789_Handle* lcd, uint8_t menuId, uint8_t index, const ScreenUiStyle& style) {
    switch (menuId) {
        case 0: ScreenDetailInputMode_Render(lcd, index, style); break;
        case 1: ScreenDetailProfiles_Render(lcd, index, style); break;
        case 2: ScreenDetailSocd_Render(lcd, index, style); break;
        case 5: ScreenDetailLightEffect_Render(lcd, index, style); break;
        case 7: ScreenDetailAmbientEffect_Render(lcd, index, style); break;
        case 4: ScreenDetailLightBrightness_Render(lcd, index, style); break;
        case 6: ScreenDetailAmbientBrightness_Render(lcd, index, style); break;
        case 8: ScreenDetailScreenBrightness_Render(lcd, index, style); break;
        case 9: ScreenDetailWebConfig_Render(lcd, index, style); break;
        case 10: ScreenDetailCalibration_Render(lcd, index, style); break;
        case 3: ScreenDetailTournament_Render(lcd, index, style); break;
        default: break;
    }
}
