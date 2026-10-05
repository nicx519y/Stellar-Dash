#include "screen_control/spi_screen_detail_entries.hpp"

#include "board_mode.hpp"
#include "board_cfg.h"
#include "storagemanager.hpp"
#include "connection_manager.hpp"
#include "system_logger.h"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control/spi_screen_ui_common.hpp"

struct ReportRateItem {
    WirelessReportRate rate;
    const char* label;
};

static const ReportRateItem kReportRates[] = {
    {RFM_RATE_1K, "1K"},
    {RFM_RATE_2K, "2K"},
    {RFM_RATE_4K, "4K"},
    {RFM_RATE_8K, "8K"},
};

static uint8_t reportRateCount(void) {
    return (uint8_t)(sizeof(kReportRates) / sizeof(kReportRates[0]));
}

static uint8_t selectedReportRateIndex(void) {
    const WirelessReportRate rate = STORAGE_MANAGER.getWirelessReportRate();
    for (uint8_t i = 0; i < reportRateCount(); i++) {
        if (kReportRates[i].rate == rate) {
            return i;
        }
    }
    return 0;
}

uint8_t ScreenDetailTournament_InitIndex(void) {
    return selectedReportRateIndex();
}

void ScreenDetailTournament_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t)reportRateCount()) {
        idx = (int32_t)reportRateCount() - 1;
    }
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailTournament_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    if (index >= reportRateCount()) index = reportRateCount() - 1u;
    ScreenDetailRender_Slider(lcd, "Report Rate", index, style,
                              reportRateCount() - 1u, kReportRates[index].label);
}

bool ScreenDetailTournament_OnConfirm(uint8_t index) {
    APP_DBG("[SCREEN][RATE] confirm index:%u", (unsigned int)index);
    if (index >= reportRateCount()) return false;
    const ReportRateItem& item = kReportRates[index];
    if (!BOARD_MODE.isStable()) {
        return false;
    }

    const bool physicalRf = BOARD_MODE.current() == BoardMode::Rf;
    const bool physicalUsb = BOARD_MODE.current() == BoardMode::Usb;
    if (!physicalRf && !physicalUsb) return false;

    /*
     * Runtime role changes are never initiated from UI. In RF position this
     * page applies the existing rate transaction before saving; in USB
     * position InputState picks up the requested rate under its existing policy.
     */
    if (physicalRf) {
        if (CONNECTION_MANAGER.getMode() != CONNECTION_MODE_RF24G ||
            !CONNECTION_MANAGER.applyWirelessReportRate(item.rate, false)) {
            APP_ERR("[SCREEN][RATE] runtime rate apply failed:%u",
                    (unsigned int)item.rate);
            return false;
        }
    }
    STORAGE_MANAGER.setWirelessReportRate(item.rate);
    ScreenUI_RequestDeferredSave(500u);
    return true;
}

bool ScreenDetailTournament_OnBack(void) {
    return true;
}
