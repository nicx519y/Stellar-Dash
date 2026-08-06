#include "screen_control/spi_screen_detail_entries.hpp"

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
    NVIC_SystemReset();
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
        "Starting secure USB WebConfig.",
        "Please wait.",
        "Hold Back to exit."
    };
    static const char* const readyLines[] = {
        "USB WebConfig is ready.",
        "Connect this device to your PC.",
        "Open the local WebConfig server.",
        "Waiting for browser authentication."
    };
    static const char* const authenticatedLines[] = {
        "Browser authenticated.",
        "WebConfig session is active.",
        "Open the local WebConfig server.",
        "Press Quit or Back to exit."
    };
    static const char* const usbModeErrorLines[] = {
        "USB switch position is required.",
        "Move the physical switch to USB.",
        "Press Retry or hold Back to exit."
    };
    static const char* const maintenanceErrorLines[] = {
        "USB maintenance is unavailable.",
        "Check the CH585 firmware and link.",
        "Press Retry or hold Back to exit."
    };
    static const char* const securityErrorLines[] = {
        "Device security check failed.",
        "Check identity and trust setup.",
        "Press Retry or hold Back to exit."
    };
    static const char* const storageInitErrorLines[] = {
        "QSPI storage is unavailable.",
        "Configuration access is disabled.",
        "Press Retry or hold Back to exit."
    };
    static const char* const storageErrorLines[] = {
        "Configuration save failed.",
        "Press Retry to save and exit again.",
        "The secure USB session is closed."
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
        case WebConfigRuntimeStatus::Starting:
        default:
            break;
    }
    ScreenDetailRender_TitleLines(lcd, "Web Config", lines, lineCount, style);
}

bool ScreenDetailWebConfig_OnConfirm(uint8_t index) {
    (void)index;
    if (WEB_CONFIG_STATE.canRetry()) {
        WEB_CONFIG_STATE.requestRetry();
        return false;
    }
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
    if (WEB_CONFIG_STATE.canRetry() ||
        WEB_CONFIG_STATE.status() == WebConfigRuntimeStatus::ErrorStorage) {
        return "Retry";
    }
    return "Quit";
}
