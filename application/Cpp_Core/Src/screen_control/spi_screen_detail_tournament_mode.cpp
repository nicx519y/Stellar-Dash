#include "screen_control/spi_screen_detail_entries.hpp"

#include <stdio.h>

#include "board_cfg.h"
#include "storagemanager.hpp"
#include "connection_manager.hpp"
#include "system_logger.h"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control/spi_screen_ui_common.hpp"

extern "C" uint32_t HAL_GetTick(void);

static constexpr uint32_t kPairPageLocalTimeoutMs = 65000u;
static constexpr uint32_t kPairPageSuccessHoldMs = 5000u;

enum class ConnectionSettingKind : uint8_t {
    Setting = 0,
    Action = 1,
};

struct ConnectionSettingItem {
    ConnectionSettingKind kind;
    ConnectionMode mode;
    WirelessReportRate rate;
    const char* label;
};

static const ConnectionSettingItem kConnectionItems[] = {
    {ConnectionSettingKind::Setting, CONNECTION_MODE_USB, RFM_RATE_1K, "USB"},
    {ConnectionSettingKind::Setting, CONNECTION_MODE_RF24G, RFM_RATE_1K, "2.4G 1K"},
    {ConnectionSettingKind::Setting, CONNECTION_MODE_RF24G, RFM_RATE_2K, "2.4G 2K"},
    {ConnectionSettingKind::Setting, CONNECTION_MODE_RF24G, RFM_RATE_4K, "2.4G 4K"},
    {ConnectionSettingKind::Setting, CONNECTION_MODE_RF24G, RFM_RATE_8K, "2.4G 8K"},
    {ConnectionSettingKind::Action, CONNECTION_MODE_RF24G, RFM_RATE_1K, "Pair 2.4G"},
};

static bool g_pairPageActive = false;
static uint32_t g_pairPageSuccessAtMs = 0u;
static bool g_pairPageSuccessConsumed = false;

static uint8_t connectionItemCount(void) {
    return (uint8_t)(sizeof(kConnectionItems) / sizeof(kConnectionItems[0]));
}

static uint8_t selectedConnectionIndex(void) {
    const ConnectionMode mode = STORAGE_MANAGER.getConnectionMode();
    const WirelessReportRate rate = STORAGE_MANAGER.getWirelessReportRate();
    for (uint8_t i = 0; i < connectionItemCount(); i++) {
        if (kConnectionItems[i].kind != ConnectionSettingKind::Setting) continue;
        if (kConnectionItems[i].mode != mode) continue;
        if (mode == CONNECTION_MODE_USB || kConnectionItems[i].rate == rate) {
            return i;
        }
    }
    return 0;
}

uint8_t ScreenDetailTournament_InitIndex(void) {
    g_pairPageActive = false;
    g_pairPageSuccessAtMs = 0u;
    g_pairPageSuccessConsumed = (CONNECTION_MANAGER.getRfPairingState() == RfPairingState::PairOk);
    return selectedConnectionIndex();
}

void ScreenDetailTournament_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    if (g_pairPageActive) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t)connectionItemCount()) {
        idx = (int32_t)connectionItemCount() - 1;
    }
    *ioIndex = (uint8_t)idx;
}

static void renderPairingPage(ST7789_Handle* lcd, const ScreenUiStyle& style, RfPairingState displayState) {
    const char* lines[4] = {0};
    char errCmdLine[12] = {0};
    char errReasonLine[12] = {0};
    char successLine[24] = {0};
    uint8_t lineCount = 0u;

    switch (displayState) {
    case RfPairingState::Starting:
        lines[lineCount++] = "Starting...";
        break;
    case RfPairingState::PairModeOn:
        lines[lineCount++] = "Pair mode on";
        lines[lineCount++] = "Waiting RX...";
        break;
    case RfPairingState::PairOk:
        lines[lineCount++] = "Pairing Success";
        if (g_pairPageSuccessAtMs != 0u) {
            const uint32_t elapsed = HAL_GetTick() - g_pairPageSuccessAtMs;
            uint32_t remainMs = (elapsed >= kPairPageSuccessHoldMs) ? 0u : (kPairPageSuccessHoldMs - elapsed);
            uint32_t remainSec = (remainMs + 999u) / 1000u;
            snprintf(successLine, sizeof(successLine), "Exit in %lus", (unsigned long)remainSec);
            lines[lineCount++] = successLine;
        }
        break;
    case RfPairingState::Timeout:
        lines[lineCount++] = "Timeout";
        break;
    case RfPairingState::TxError:
        lines[lineCount++] = "TX Error";
        snprintf(errCmdLine, sizeof(errCmdLine), "cmd %02X",
                 (unsigned int)CONNECTION_MANAGER.getRfPairingLastErrorCommand());
        snprintf(errReasonLine, sizeof(errReasonLine), "reason %02X",
                 (unsigned int)CONNECTION_MANAGER.getRfPairingLastErrorReason());
        lines[lineCount++] = errCmdLine;
        lines[lineCount++] = errReasonLine;
        break;
    case RfPairingState::Idle:
    default:
        lines[lineCount++] = "Ready";
        break;
    }

    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(ST7789_WIDTH - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t x = (uint16_t)(listX + 8u);
    const uint8_t titleScale = 2u;
    const uint8_t textScale = SPI_SCREEN_MENU_TEXT_SCALE;
    const uint16_t titleY = 8u;
    const uint16_t titleH = ScreenUI_CharCellH(titleScale);
    const uint16_t lineH = ScreenUI_CharCellH(textScale);
    const uint16_t gap = 4u;
    uint16_t y = (uint16_t)(titleY + titleH + 12u);

    ST7789_FillRect(lcd, listX, 0, listW, ST7789_HEIGHT, style.bg);
    ST7789_DrawString(lcd, x, titleY, "Pair 2.4G", style.text, style.bg, titleScale);

    for (uint8_t i = 0; i < lineCount; i++) {
        if (!lines[i]) continue;
        if (y + lineH > ST7789_HEIGHT) break;
        ST7789_DrawString(lcd, x, y, lines[i], style.text, style.bg, textScale);
        y = (uint16_t)(y + lineH + gap);
    }
}

void ScreenDetailTournament_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    const uint32_t now = HAL_GetTick();
    const RfPairingState currentPairState = CONNECTION_MANAGER.getRfPairingState();
    if (!g_pairPageActive &&
        !g_pairPageSuccessConsumed &&
        currentPairState == RfPairingState::PairOk) {
        g_pairPageActive = true;
        g_pairPageSuccessAtMs = now;
        APP_DBG("[SCREEN][PAIR] success latch from inactive page");
    }

    if (g_pairPageActive) {
        RfPairingState pairState = (g_pairPageSuccessAtMs != 0u) ?
                                   RfPairingState::PairOk :
                                   currentPairState;
        const uint32_t startedAt = CONNECTION_MANAGER.getRfPairingStartedAtMs();
        if ((pairState == RfPairingState::PairModeOn ||
             pairState == RfPairingState::Starting) &&
            startedAt != 0u &&
            (uint32_t)(now - startedAt) >= kPairPageLocalTimeoutMs) {
            APP_DBG("[SCREEN][PAIR] local timeout leave state:%u", (unsigned int)pairState);
            pairState = RfPairingState::Timeout;
        }
        if (pairState == RfPairingState::PairOk) {
            if (g_pairPageSuccessAtMs == 0u) {
                g_pairPageSuccessAtMs = now;
                APP_DBG("[SCREEN][PAIR] success hold start");
            }
            if ((uint32_t)(now - g_pairPageSuccessAtMs) >= kPairPageSuccessHoldMs) {
                APP_DBG("[SCREEN][PAIR] success leave");
                g_pairPageActive = false;
                g_pairPageSuccessAtMs = 0u;
                g_pairPageSuccessConsumed = true;
            } else {
                renderPairingPage(lcd, style, pairState);
                return;
            }
        } else {
            g_pairPageSuccessAtMs = 0u;
        }
        if (!g_pairPageActive) {
            /* Success hold has completed; fall through to the Connection list. */
        } else if (pairState == RfPairingState::Timeout) {
            APP_DBG("[SCREEN][PAIR] auto leave state:%u", (unsigned int)pairState);
            g_pairPageActive = false;
        } else {
            renderPairingPage(lcd, style, pairState);
            return;
        }
    }

    if (g_pairPageActive) {
        renderPairingPage(lcd, style, CONNECTION_MANAGER.getRfPairingState());
        return;
    }

    const char* labels[sizeof(kConnectionItems) / sizeof(kConnectionItems[0])] = {0};
    for (uint8_t i = 0; i < connectionItemCount(); i++) {
        labels[i] = kConnectionItems[i].label;
    }
    const uint8_t selected = selectedConnectionIndex();
    ScreenDetailRender_List(lcd, "Connection", labels, connectionItemCount(), index, selected, style);
}

bool ScreenDetailTournament_OnConfirm(uint8_t index) {
    APP_DBG("[SCREEN][CONN] confirm index:%u pair_page:%u",
            (unsigned int)index,
            (unsigned int)(g_pairPageActive ? 1u : 0u));
    if (g_pairPageActive) {
        return false;
    }
    if (index >= connectionItemCount()) return false;
    const ConnectionSettingItem& item = kConnectionItems[index];
    if (item.kind == ConnectionSettingKind::Action) {
        APP_DBG("[SCREEN][PAIR] action selected");
        g_pairPageActive = true;
        g_pairPageSuccessAtMs = 0u;
        g_pairPageSuccessConsumed = false;
        (void)CONNECTION_MANAGER.startRfPairing();
        return false;
    }

    const ConnectionMode runtimeMode = CONNECTION_MANAGER.getMode();
    STORAGE_MANAGER.setConnectionMode(item.mode);
    if (item.mode == CONNECTION_MODE_RF24G) {
        STORAGE_MANAGER.setWirelessReportRate(item.rate);
        STORAGE_MANAGER.setInputMode(INPUT_MODE_XINPUT);
        if (runtimeMode == CONNECTION_MODE_RF24G) {
            if (!CONNECTION_MANAGER.applyWirelessReportRate(item.rate, false)) {
                APP_ERR("[SCREEN][CONN] runtime rate apply failed:%u", (unsigned int)item.rate);
            }
        } else {
            APP_DBG("[SCREEN][CONN] mode switch USB->RF24G requires reboot rate:%u",
                    (unsigned int)item.rate);
            ScreenUI_RequestRebootTo(3u, index);
            return false;
        }
    } else if (runtimeMode != CONNECTION_MODE_USB) {
        APP_DBG("[SCREEN][CONN] mode switch RF24G->USB requires reboot");
        ScreenUI_RequestRebootTo(3u, index);
        return false;
    }
    ScreenUI_RequestDeferredSave(500u);
    return true;
}

bool ScreenDetailTournament_OnBack(void) {
    if (g_pairPageActive) {
        APP_DBG("[SCREEN][PAIR] back stop");
        (void)CONNECTION_MANAGER.stopRfPairing();
        g_pairPageActive = false;
        g_pairPageSuccessAtMs = 0u;
        g_pairPageSuccessConsumed = false;
    }
    return true;
}
