#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "connection_mode_config.hpp"
#include "storagemanager.hpp"
#include "states/input_state.hpp"
#include "main_runtime_control.hpp"

// UI actions are not timing-critical; keep RAM-resident code compact.
#if defined(__GNUC__)
#pragma GCC optimize("Os")
#endif

namespace {
const char* const labels[] = {"USB", "2.4G RF"};
bool saveFailed = false;
}

uint8_t ScreenDetailConnection_InitIndex()
{
    saveFailed = false;
    return ConnectionConfig::selected(STORAGE_MANAGER.config) == CONNECTION_MODE_RF24G ? 1u : 0u;
}

void ScreenDetailConnection_Rotate(uint8_t* index, int8_t det)
{
    if (!index) return;
    *index = ScreenList_Rotate(*index, det, 2u);
}

bool ScreenDetailConnection_OnConfirm(uint8_t index)
{
    if (index > 1u || MainRuntime_IsTxIspActive()) return false;
    const auto mode = index == 1u ? CONNECTION_MODE_RF24G : CONNECTION_MODE_USB;
    auto& config = STORAGE_MANAGER.config;
    const auto previousMode = ConnectionConfig::selected(config);
    if (mode == previousMode) return true;
    const auto previousBootMode = STORAGE_MANAGER.getBootMode();
    const bool wasRunning = INPUT_STATE.suspendInputPipelineForStorage();
    // A failed journal save can reload an A1 bank. Normalize its screen
    // schema before setting the new preference, so save() will not migrate
    // that deliberate choice back to USB on a retry.
    ScreenConfig::normalize(config.screenControl);
    STORAGE_MANAGER.setConnectionMode(mode);
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    if (!STORAGE_MANAGER.saveConfig()) {
        STORAGE_MANAGER.setConnectionMode(previousMode);
        STORAGE_MANAGER.setBootMode(previousBootMode);
        (void)INPUT_STATE.resumeInputPipelineAfterStorage(wasRunning);
        saveFailed = true;
        return false;
    }
    saveFailed = false;
    MainRuntime_RequestReset();
    return true;
}

void ScreenDetailConnection_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style)
{
    const uint8_t selected = ConnectionConfig::selected(STORAGE_MANAGER.config) == CONNECTION_MODE_RF24G ? 1u : 0u;
    ScreenDetailRender_List(lcd, saveFailed ? "Save Failed" : "Connection", labels,
        2u, index, selected, style);
}
