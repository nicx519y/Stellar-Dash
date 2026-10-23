// Menu dispatch/rendering is outside the input hot path. Keep RAM-resident UI compact.
#if defined(__GNUC__)
#pragma GCC optimize("Os")
#endif

#include "screen_control/spi_screen_detail_pages.hpp"

#include "screen_control/spi_screen_detail_entries.hpp"

#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control_config.hpp"
#include "storagemanager.hpp"
#include "board_cfg.h"
#include "board_mode.hpp"
#include "battery_test_log.h"
#include <cstdio>

namespace {
// Internal children must not alias persistent Connection menu ID 15.
constexpr uint8_t kScreenTimer = 16, kSleepTimer = 17;
constexpr uint8_t kBatteryTest = 18, kChargeTest = 19;
bool isBatteryTest(uint8_t id) { return id==kBatteryTest || id==kChargeTest; }
uint8_t batteryPage = 0; // RAM-only: list, start confirmation, status
constexpr uint16_t kTimes[] = {10, 30, 60, 120, 300};
constexpr const char* kTimeLabels[] = {"10s", "30s", "1min", "2min", "5min"};
uint8_t groupSelection = 0, childPage = 255, childIndex = 0;
bool isGroup(uint8_t id) { return id == ScreenConfig::kLed || id == ScreenConfig::kPower; }
bool isTimer(uint8_t id) { return id == kScreenTimer || id == kSleepTimer; }
uint8_t groupCount(uint8_t) { return 4; }
uint8_t groupChild(uint8_t id, uint8_t index) { return id == ScreenConfig::kLed ? uint8_t(4 + index) : uint8_t(kScreenTimer + index); }
bool timerSupported(uint8_t id) { return id == kScreenTimer || HBOX_AUTO_SLEEP_ENABLED != 0; }
bool timerEnabled(uint8_t id) {
    return id == kScreenTimer ? ScreenConfig::enabled(STORAGE_MANAGER.config.screenControl) : STORAGE_MANAGER.config.power.autoSleepEnabled == 1;
}
}

ScreenDetailKind ScreenDetail_Kind(uint8_t menuId) {
    if(isBatteryTest(menuId)) return batteryPage?SCREEN_DETAIL_INFO:SCREEN_DETAIL_LIST;
    if (isGroup(menuId)) return childPage == 255 ? SCREEN_DETAIL_LIST : ScreenDetail_Kind(childPage);
    if (isTimer(menuId)) return timerSupported(menuId) ? SCREEN_DETAIL_SLIDER : SCREEN_DETAIL_INFO;
    if (menuId == 2 && ScreenDetailSocd_WarningVisible()) return SCREEN_DETAIL_INFO;
    if (menuId == 11 && ScreenDetailButtonsPerformance_IsEditing()) return SCREEN_DETAIL_SLIDER;
    switch (menuId) {
        case ScreenConfig::kConnection:
        case 0:
        case 1:
        case 2:
        case 11:
        case 5:
        case 7:
            return SCREEN_DETAIL_LIST;
        case 3:
        case 4:
        case 6:
        case 8:
            return SCREEN_DETAIL_SLIDER;
        case 9:
        case 10:
        case SCREEN_MENU_TX_ISP:
            return SCREEN_DETAIL_INFO;
        default:
            return SCREEN_DETAIL_NONE;
    }
}

const char* ScreenDetail_ConfirmLabel(uint8_t menuId, uint8_t index) {
    if(isBatteryTest(menuId) && batteryPage) return batteryPage==1?"Start":"Back";
    if (isGroup(menuId)) {
        if (childPage != 255) return ScreenDetail_ConfirmLabel(childPage, childIndex);
        return groupSelection == SCREEN_LIST_BACK ? "Back" : "Open";
    }
    if (menuId == 11) return ScreenDetailButtonsPerformance_ConfirmLabel(index);
    if (menuId == 2 && ScreenDetailSocd_WarningVisible()) return "Close";
    if (ScreenDetail_Kind(menuId) == SCREEN_DETAIL_LIST && index == SCREEN_LIST_BACK) return "Back";
    if (isTimer(menuId)) return !timerSupported(menuId) ? "N/A" : timerEnabled(menuId) ? "OFF" : "ON";
    if (menuId == 9) return ScreenDetailWebConfig_ConfirmLabel();
    if (menuId == 10) return "Quit";
    if (menuId == SCREEN_MENU_TX_ISP) return ScreenDetailTxIsp_ConfirmLabel();
    if (menuId == 0 && (!BOARD_MODE.isStable() || BOARD_MODE.current() != BoardMode::Usb)) return "Back";
    return "Apply";
}

const char* ScreenDetail_BackLabel(uint8_t menuId) {
    if (menuId == SCREEN_MENU_TX_ISP) return ScreenDetailTxIsp_BackLabel();
    return "Back";
}

uint8_t ScreenDetail_InitIndex(uint8_t menuId) {
    if(isBatteryTest(menuId)) { batteryPage=0; return 0; }
    if (isGroup(menuId)) { groupSelection = 0; childPage = 255; return 0; }
    if (isTimer(menuId)) {
        const uint32_t seconds = menuId == kScreenTimer ? STORAGE_MANAGER.config.screenControl.standbyTimeoutSeconds : STORAGE_MANAGER.config.power.autoStandbyMs / 1000u;
        for (uint8_t i = 0; i < 5; ++i) if (seconds == kTimes[i]) return i;
        return menuId == kScreenTimer ? 0 : 4;
    }
    switch (menuId) {
        case ScreenConfig::kConnection: return ScreenDetailConnection_InitIndex();
        case 0: return ScreenDetailInputMode_InitIndex();
        case 1: return ScreenDetailProfiles_InitIndex();
        case 2: return ScreenDetailSocd_InitIndex();
        case 11: return ScreenDetailButtonsPerformance_InitIndex();
        case 5: return ScreenDetailLightEffect_InitIndex();
        case 7: return ScreenDetailAmbientEffect_InitIndex();
        case 4: return ScreenDetailLightBrightness_InitIndex();
        case 6: return ScreenDetailAmbientBrightness_InitIndex();
        case 8: return ScreenDetailScreenBrightness_InitIndex();
        case 9: return ScreenDetailWebConfig_InitIndex();
        case 10: return ScreenDetailCalibration_InitIndex();
        case SCREEN_MENU_TX_ISP: return ScreenDetailTxIsp_InitIndex();
        case 3: return ScreenDetailTournament_InitIndex();
        default: return 0;
    }
}

void ScreenDetail_OnRotate(uint8_t menuId, uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    if(isBatteryTest(menuId)) { if(!batteryPage) *ioIndex=ScreenList_Rotate(*ioIndex,det,3); return; }
    if (isGroup(menuId)) {
        if (childPage != 255) ScreenDetail_OnRotate(childPage, &childIndex, det);
        else {
            groupSelection = ScreenList_Rotate(groupSelection, det, groupCount(menuId));
            *ioIndex = groupSelection;
        }
        return;
    }
    if (isTimer(menuId)) {
        if (!timerSupported(menuId)) return;
        int next = int(*ioIndex) + det; next = next < 0 ? 0 : next > 4 ? 4 : next;
        if (*ioIndex == next) return;
        *ioIndex = uint8_t(next);
        if (menuId == kScreenTimer) STORAGE_MANAGER.config.screenControl.standbyTimeoutSeconds = kTimes[next];
        else STORAGE_MANAGER.config.power.autoStandbyMs = uint32_t(kTimes[next]) * 1000u;
        ScreenUI_RequestDeferredSave(2000u);
        return;
    }
    switch (menuId) {
        case ScreenConfig::kConnection: ScreenDetailConnection_Rotate(ioIndex, det); break;
        case 0: ScreenDetailInputMode_Rotate(ioIndex, det); break;
        case 1: ScreenDetailProfiles_Rotate(ioIndex, det); break;
        case 2: ScreenDetailSocd_Rotate(ioIndex, det); break;
        case 11: ScreenDetailButtonsPerformance_Rotate(ioIndex, det); break;
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
    if(isBatteryTest(menuId)) {
        if(batteryPage==1) {
            if(menuId==kChargeTest) { if(ChargeTest_RequestStart) ChargeTest_RequestStart(); }
            else if(BatteryTest_RequestStart) BatteryTest_RequestStart();
            batteryPage=2; return false;
        }
        if(batteryPage==2) { batteryPage=0; return false; }
        if(index==SCREEN_LIST_BACK) return true;
        if(index==0) batteryPage=1;
        else { if(index==2 && BatteryTest_RequestEnd) BatteryTest_RequestEnd(); batteryPage=2; }
        return false;
    }
    if (isGroup(menuId)) {
        if (childPage == 255) {
            if (groupSelection == SCREEN_LIST_BACK) return true;
            childPage = groupChild(menuId, groupSelection); childIndex = ScreenDetail_InitIndex(childPage);
        }
        else if (ScreenDetail_OnConfirm(childPage, childIndex)) childPage = 255;
        return false;
    }
    if (menuId != 11 && ScreenDetail_Kind(menuId) == SCREEN_DETAIL_LIST && index == SCREEN_LIST_BACK)
        return ScreenDetail_OnBack(menuId);
    if (isTimer(menuId)) {
        if (!timerSupported(menuId)) return false;
        if (menuId == kScreenTimer) ScreenConfig::setEnabled(STORAGE_MANAGER.config.screenControl, !timerEnabled(menuId));
        else STORAGE_MANAGER.config.power.autoSleepEnabled = timerEnabled(menuId) ? 0u : 1u;
        ScreenUI_RequestDeferredSave(2000u);
        return false;
    }
    switch (menuId) {
        case ScreenConfig::kConnection: return ScreenDetailConnection_OnConfirm(index);
        case 0: ScreenDetailInputMode_OnConfirm(index); return true;
        case 1: ScreenDetailProfiles_OnConfirm(index); return true;
        case 2: ScreenDetailSocd_OnConfirm(index); return ScreenDetailSocd_ShouldExitAfterConfirm();
        case 11: return ScreenDetailButtonsPerformance_OnConfirm(index);
        case 5: ScreenDetailLightEffect_OnConfirm(index); return true;
        case 7: ScreenDetailAmbientEffect_OnConfirm(index); return true;
        case 4: ScreenDetailLightBrightness_OnConfirm(index); return true;
        case 6: ScreenDetailAmbientBrightness_OnConfirm(index); return true;
        case 8: ScreenDetailScreenBrightness_OnConfirm(index); return true;
        case 9: return ScreenDetailWebConfig_OnConfirm(index);
        case 10: ScreenDetailCalibration_OnConfirm(index); return true;
        case SCREEN_MENU_TX_ISP: return ScreenDetailTxIsp_OnConfirm(index);
        case 3: return ScreenDetailTournament_OnConfirm(index);
        default: return false;
    }
}

bool ScreenDetail_OnBack(uint8_t menuId) {
    if(isBatteryTest(menuId) && batteryPage) { batteryPage=0; return false; }
    if (isGroup(menuId)) {
        if (childPage == 255) return true;
        if (ScreenDetail_OnBack(childPage)) childPage = 255;
        return false;
    }
    switch (menuId) {
        case 9: return ScreenDetailWebConfig_OnBack();
        case SCREEN_MENU_TX_ISP: return ScreenDetailTxIsp_OnBack();
        case 3: return ScreenDetailTournament_OnBack();
        case 11: return !ScreenDetailButtonsPerformance_OnBack();
        default: return true;
    }
}

void ScreenDetail_Render(ST7789_Handle* lcd, uint8_t menuId, uint8_t index, const ScreenUiStyle& style) {
    if(isBatteryTest(menuId)) {
        if(batteryPage==1) {
            const char *lines[]={"Record for up to 24 hours.","Writes briefly pause input.","Settings and logs are kept."};
            ScreenDetailRender_TitleLines(lcd,menuId==kChargeTest?"Start Charge Test":"Start Battery Test",lines,3,style);
        } else if(batteryPage==2) {
            char elapsed[32]; snprintf(elapsed,sizeof(elapsed),"Elapsed: %lus",
                (unsigned long)(BatteryTest_Elapsed?BatteryTest_Elapsed()/1000u:0));
            const char *lines[]={BatteryTest_Status?BatteryTest_Status():"Unavailable",elapsed,"Logs: WebConfig"};
            const bool charging=BatteryTest_Active && BatteryTest_Active()?
                BatteryTest_IsCharge && BatteryTest_IsCharge():menuId==kChargeTest;
            ScreenDetailRender_TitleLines(lcd,charging?"Charge Test":"Battery Test",lines,3,style);
        } else {
            const char *labels[]={"Start Test","View Status","End Test"};
            ScreenDetailRender_List(lcd,menuId==kChargeTest?"Charge Test":"Battery Test",labels,3,index,index,style);
        }
        return;
    }
    if (isGroup(menuId)) {
        if (childPage != 255) { ScreenDetail_Render(lcd, childPage, childIndex, style); return; }
        static const char* ledLabels[] = {"LED Brightness", "LED Effect", "Ambient Brightness", "Ambient Effect"};
        static const char* powerLabels[] = {"Screen Standby", "Auto Sleep", "Battery Test", "Charge Test"};
        ScreenDetailRender_List(lcd, menuId == ScreenConfig::kLed ? "LED Setting" : "Power",
            menuId == ScreenConfig::kLed ? ledLabels : powerLabels, groupCount(menuId), groupSelection, groupSelection, style);
        return;
    }
    if (isTimer(menuId)) {
        char label[24];
        // Read current values so WebConfig changes do not leave stale labels.
        index = ScreenDetail_InitIndex(menuId); childIndex = index;
        snprintf(label, sizeof(label), "%s %s", kTimeLabels[index], !timerSupported(menuId) ? "Unavailable" : timerEnabled(menuId) ? "ON" : "OFF");
        ScreenDetailRender_Slider(lcd, menuId == kScreenTimer ? "Screen Standby" : "Auto Sleep", index, style, 4, label);
        return;
    }
    switch (menuId) {
        case ScreenConfig::kConnection: ScreenDetailConnection_Render(lcd, index, style); break;
        case 0: ScreenDetailInputMode_Render(lcd, index, style); break;
        case 1: ScreenDetailProfiles_Render(lcd, index, style); break;
        case 2: ScreenDetailSocd_Render(lcd, index, style); break;
        case 11: ScreenDetailButtonsPerformance_Render(lcd, index, style); break;
        case 5: ScreenDetailLightEffect_Render(lcd, index, style); break;
        case 7: ScreenDetailAmbientEffect_Render(lcd, index, style); break;
        case 4: ScreenDetailLightBrightness_Render(lcd, index, style); break;
        case 6: ScreenDetailAmbientBrightness_Render(lcd, index, style); break;
        case 8: ScreenDetailScreenBrightness_Render(lcd, index, style); break;
        case 9: ScreenDetailWebConfig_Render(lcd, index, style); break;
        case 10: ScreenDetailCalibration_Render(lcd, index, style); break;
        case SCREEN_MENU_TX_ISP: ScreenDetailTxIsp_Render(lcd, index, style); break;
        case 3: ScreenDetailTournament_Render(lcd, index, style); break;
        default: break;
    }
}
