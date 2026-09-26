#include "screen_control/spi_screen_detail_entries.hpp"

#include "board_mode.hpp"
#include "board_cfg.h"
#include "storagemanager.hpp"
#include "connection_manager.hpp"
#include "system_logger.h"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control/spi_screen_ui_common.hpp"

struct ConnectionSettingItem {
    ConnectionMode mode;
    WirelessReportRate rate;
    const char* label;
};

static const ConnectionSettingItem kConnectionItems[] = {
    {CONNECTION_MODE_USB, RFM_RATE_1K, "USB"},
    {CONNECTION_MODE_RF24G, RFM_RATE_1K, "2.4G 1K"},
    {CONNECTION_MODE_RF24G, RFM_RATE_2K, "2.4G 2K"},
    {CONNECTION_MODE_RF24G, RFM_RATE_4K, "2.4G 4K"},
    {CONNECTION_MODE_RF24G, RFM_RATE_8K, "2.4G 8K"},
};

static uint8_t connectionItemCount(void) {
    return (uint8_t)(sizeof(kConnectionItems) / sizeof(kConnectionItems[0]));
}

static uint8_t selectedConnectionIndex(void) {
    const ConnectionMode mode =
        (BOARD_MODE.isStable() &&
         BOARD_MODE.current() == BoardMode::Rf)
            ? CONNECTION_MODE_RF24G
            : CONNECTION_MODE_USB;
    const WirelessReportRate rate = STORAGE_MANAGER.getWirelessReportRate();
    for (uint8_t i = 0; i < connectionItemCount(); i++) {
        if (kConnectionItems[i].mode != mode) continue;
        if (mode == CONNECTION_MODE_USB || kConnectionItems[i].rate == rate) {
            return i;
        }
    }
    return 0;
}

uint8_t ScreenDetailTournament_InitIndex(void) {
    return selectedConnectionIndex();
}

void ScreenDetailTournament_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t)connectionItemCount()) {
        idx = (int32_t)connectionItemCount() - 1;
    }
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailTournament_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    const char* labels[sizeof(kConnectionItems) / sizeof(kConnectionItems[0])] = {0};
    for (uint8_t i = 0; i < connectionItemCount(); i++) {
        labels[i] = kConnectionItems[i].label;
    }
    const uint8_t selected = selectedConnectionIndex();
    ScreenDetailRender_List(lcd, "Connection", labels, connectionItemCount(), index, selected, style);
}

bool ScreenDetailTournament_OnConfirm(uint8_t index) {
    APP_DBG("[SCREEN][CONN] confirm index:%u", (unsigned int)index);
    if (index >= connectionItemCount()) return false;
    const ConnectionSettingItem& item = kConnectionItems[index];
    if (!BOARD_MODE.isStable()) {
        return false;
    }

    const bool physicalRf = BOARD_MODE.current() == BoardMode::Rf;
    const bool physicalUsb = BOARD_MODE.current() == BoardMode::Usb;
    if ((item.mode == CONNECTION_MODE_RF24G && !physicalRf) ||
        (item.mode == CONNECTION_MODE_USB && !physicalUsb)) {
        APP_ERR("[SCREEN][CONN] role is controlled by physical switch");
        return false;
    }

    /*
     * Runtime role changes are never initiated from UI. In RF position this
     * page may only apply the existing frozen rate transaction; in USB
     * position the USB row is informational.
     */
    if (physicalRf) {
        if (CONNECTION_MANAGER.getMode() != CONNECTION_MODE_RF24G ||
            !CONNECTION_MANAGER.applyWirelessReportRate(item.rate, false)) {
            APP_ERR("[SCREEN][CONN] runtime rate apply failed:%u",
                    (unsigned int)item.rate);
            return false;
        }
        STORAGE_MANAGER.setWirelessReportRate(item.rate);
        STORAGE_MANAGER.setConnectionMode(CONNECTION_MODE_RF24G);
    } else {
        STORAGE_MANAGER.setConnectionMode(CONNECTION_MODE_USB);
    }
    ScreenUI_RequestDeferredSave(500u);
    return true;
}

bool ScreenDetailTournament_OnBack(void) {
    return true;
}
