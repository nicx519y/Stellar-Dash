#include "screen_control/spi_screen_detail_entries.hpp"
#include "main_runtime_control.hpp"

#include "board_cfg.h"
#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "states/webconfig_state.hpp"

namespace {

static bool exitWebConfig()
{
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    if (!STORAGE_MANAGER.saveConfig()) {
        /* Keep the in-memory mode aligned with the last committed journal. */
        STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_WEB_CONFIG);
        WEB_CONFIG_STATE.reportStorageFailure();
        return false;
    }
    MainRuntime_RequestReset();
    return true;
}

} // namespace

uint8_t ScreenDetailWebConfig_InitIndex(void) {
    return 0;
}

void ScreenDetailWebConfig_Rotate(uint8_t* ioIndex, int8_t det) {
    (void)ioIndex;
    (void)det;
}

void ScreenDetailWebConfig_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    (void)index;
    static const char* const startingLines[] = {
        "Starting USB...",
        "Please wait.",
        "Hold Back to exit."
    };
    static const char* const readyLines[] = {
        "Connect USB to your PC.",
        "Open WebConfig in browser.",
        "Waiting for authentication."
    };
    static const char* const authenticatedLines[] = {
        "Browser authenticated.",
        "Press knob to exit."
    };
    static const char* const usbModeErrorLines[] = {
        "USB unavailable.",
        "Reopen Web Config to retry.",
        "Press knob to exit."
    };
    static const char* const maintenanceErrorLines[] = {
        "USB maintenance unavailable.",
        "Check CH585 firmware/link.",
        "Reopen Web Config to retry.",
        "Press knob to exit."
    };
    static const char* const securityErrorLines[] = {
        "Security check failed.",
        "Check identity/trust setup.",
        "Press knob to exit."
    };
    static const char* const storageInitErrorLines[] = {
        "Storage unavailable.",
        "Config access disabled.",
        "Press knob to exit."
    };
    static const char* const adcErrorLines[] = {
        "Input sampling stopped.",
        "USB session closed.",
        "Press knob to exit."
    };
    static const char* const storageErrorLines[] = {
        "Settings could not be saved.",
        "USB session closed.",
        "Press knob to save and exit."
    };

    const char* const* lines = startingLines;
    uint8_t lineCount = (uint8_t)(sizeof(startingLines) / sizeof(startingLines[0]));
    switch (WEB_CONFIG_STATE.status()) {
        case WebConfigRuntimeStatus::Ready:
            lines = readyLines;
            lineCount = (uint8_t)(sizeof(readyLines) / sizeof(readyLines[0]));
            break;
        case WebConfigRuntimeStatus::Authenticated:
            lines = authenticatedLines;
            lineCount = (uint8_t)(sizeof(authenticatedLines) / sizeof(authenticatedLines[0]));
            break;
        case WebConfigRuntimeStatus::ErrorUsbMode:
            lines = usbModeErrorLines;
            lineCount = (uint8_t)(sizeof(usbModeErrorLines) / sizeof(usbModeErrorLines[0]));
            break;
        case WebConfigRuntimeStatus::ErrorMaintenance:
            lines = maintenanceErrorLines;
            lineCount = (uint8_t)(sizeof(maintenanceErrorLines) / sizeof(maintenanceErrorLines[0]));
            break;
        case WebConfigRuntimeStatus::ErrorSecurity:
            lines = securityErrorLines;
            lineCount = (uint8_t)(sizeof(securityErrorLines) / sizeof(securityErrorLines[0]));
            break;
        case WebConfigRuntimeStatus::ErrorStorageInit:
            lines = storageInitErrorLines;
            lineCount = (uint8_t)(sizeof(storageInitErrorLines) / sizeof(storageInitErrorLines[0]));
            break;
        case WebConfigRuntimeStatus::ErrorStorage:
            lines = storageErrorLines;
            lineCount = (uint8_t)(sizeof(storageErrorLines) / sizeof(storageErrorLines[0]));
            break;
        case WebConfigRuntimeStatus::ErrorAdc:
            lines = adcErrorLines;
            lineCount = (uint8_t)(sizeof(adcErrorLines) / sizeof(adcErrorLines[0]));
            break;
        case WebConfigRuntimeStatus::Starting:
        default:
            break;
    }
    ScreenDetailRender_TitleLines(lcd, "Web Config", lines, lineCount, style);
}

bool ScreenDetailWebConfig_OnConfirm(uint8_t index) {
    (void)index;
    if (WEB_CONFIG_STATE.status() == WebConfigRuntimeStatus::Starting) {
        return false;
    }
    return exitWebConfig();
}

bool ScreenDetailWebConfig_OnBack(void) {
    return exitWebConfig();
}

const char* ScreenDetailWebConfig_ConfirmLabel(void)
{
    if (WEB_CONFIG_STATE.status() == WebConfigRuntimeStatus::Starting) {
        return "Wait";
    }
    if (WEB_CONFIG_STATE.status() == WebConfigRuntimeStatus::ErrorStorage) {
        return "Retry";
    }
    return "Quit";
}
