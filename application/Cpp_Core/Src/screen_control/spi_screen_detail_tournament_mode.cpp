#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "connection_manager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

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

uint8_t ScreenDetailTournament_InitIndex(void) {
    const ConnectionMode mode = STORAGE_MANAGER.getConnectionMode();
    const WirelessReportRate rate = STORAGE_MANAGER.getWirelessReportRate();
    for (uint8_t i = 0; i < (uint8_t)(sizeof(kConnectionItems) / sizeof(kConnectionItems[0])); i++) {
        if (kConnectionItems[i].mode != mode) continue;
        if (mode == CONNECTION_MODE_USB || kConnectionItems[i].rate == rate) {
            return i;
        }
    }
    return 0;
}

void ScreenDetailTournament_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t)(sizeof(kConnectionItems) / sizeof(kConnectionItems[0]))) {
        idx = (int32_t)(sizeof(kConnectionItems) / sizeof(kConnectionItems[0])) - 1;
    }
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailTournament_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    const char* labels[sizeof(kConnectionItems) / sizeof(kConnectionItems[0])] = {0};
    for (uint8_t i = 0; i < (uint8_t)(sizeof(kConnectionItems) / sizeof(kConnectionItems[0])); i++) {
        labels[i] = kConnectionItems[i].label;
    }
    const uint8_t selected = ScreenDetailTournament_InitIndex();
    ScreenDetailRender_List(lcd, "Connection", labels, (uint8_t)(sizeof(labels) / sizeof(labels[0])), index, selected, style);
}

void ScreenDetailTournament_OnConfirm(uint8_t index) {
    if (index >= (uint8_t)(sizeof(kConnectionItems) / sizeof(kConnectionItems[0]))) return;
    const ConnectionSettingItem& item = kConnectionItems[index];
    STORAGE_MANAGER.setConnectionMode(item.mode);
    if (item.mode == CONNECTION_MODE_RF24G) {
        STORAGE_MANAGER.setWirelessReportRate(item.rate);
        STORAGE_MANAGER.setInputMode(INPUT_MODE_XINPUT);
        if (CONNECTION_MANAGER.getMode() == CONNECTION_MODE_RF24G) {
            (void)CONNECTION_MANAGER.applyWirelessReportRate(item.rate, false);
        }
    }
    ScreenUI_RequestDeferredSave(500u);
}
