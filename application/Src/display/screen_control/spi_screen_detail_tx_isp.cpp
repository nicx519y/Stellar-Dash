#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "main_runtime_control.hpp"

namespace {
bool confirmExit = false;
bool entryRejected = false;
}

uint8_t ScreenDetailTxIsp_InitIndex()
{
    confirmExit = entryRejected = false;
    return 0u;
}

void ScreenDetailTxIsp_Render(ST7789_Handle* lcd, uint8_t, const ScreenUiStyle& style)
{
    static const char* const entry[] = {
        "Connect TX PB22 to GND first.",
        "Press Start to restart TX in ISP.",
        "USB/RF/WebConfig will pause.",
        "Use WCHISPStudio after entry."
    };
    static const char* const active[] = {
        "TX power held on. SPI paused.",
        "Use WCHISPStudio to download.",
        "Keep power on during download.",
        "Press Finish after it completes."
    };
    static const char* const leaving[] = {
        "Ensure the download has ended.",
        "Remove PB22 from GND first.",
        "Press Exit to return to prior mode.",
        "Hold Back to keep ISP active."
    };
    static const char* const blocked[] = {
        "Firmware update owns the device.",
        "ISP was not started.",
        "Finish the update before retrying.",
        "Hold Back to return."
    };
    static const char* const failed[] = {
        "Mode change failed. Try again.",
        "Current mode has been kept.",
        "Check storage, then retry.",
        "Confirm again to retry."
    };
    const char* const* lines = MainRuntime_TxIspTransitionFailed() ? failed : !MainRuntime_IsTxIspActive()
        ? (entryRejected ? blocked : entry) : (confirmExit ? leaving : active);
    ScreenDetailRender_TitleLines(lcd, "TX ISP", lines, 4u, style);
}

bool ScreenDetailTxIsp_OnConfirm(uint8_t)
{
    if (!MainRuntime_IsTxIspActive()) {
        entryRejected = !MainRuntime_RequestTxIsp(true);
        return false;
    }
    if (!confirmExit) { confirmExit = true; return false; }
    return MainRuntime_RequestTxIsp(false);
}

bool ScreenDetailTxIsp_OnBack()
{
    if (!MainRuntime_IsTxIspActive()) return true;
    // Back never cuts power: it opens/cancels the explicit Exit confirmation.
    confirmExit = !confirmExit;
    return false;
}

const char* ScreenDetailTxIsp_ConfirmLabel()
{
    return !MainRuntime_IsTxIspActive() ? "Start" : (confirmExit ? "Exit" : "Finish");
}
