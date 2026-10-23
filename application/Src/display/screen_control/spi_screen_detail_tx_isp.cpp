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
        "First: TX PB22 to GND.",
        "Press knob to enter ISP.",
        "USB/RF/WebConfig will pause.",
        "Then open WCHISPStudio."
    };
    static const char* const active[] = {
        "TX powered; SPI paused.",
        "Flash with WCHISPStudio.",
        "Keep power on during flash.",
        "Press knob when complete."
    };
    static const char* const leaving[] = {
        "Wait for flashing to finish.",
        "Disconnect PB22 from GND.",
        "Press knob to exit ISP.",
        "Hold Back to stay in ISP."
    };
    static const char* const blocked[] = {
        "Firmware update in progress.",
        "ISP has not started.",
        "Finish update, then retry.",
        "Hold Back to return."
    };
    static const char* const failed[] = {
        "Mode change failed.",
        "Current mode kept.",
        "Check storage, then retry.",
        "Press knob to retry."
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

const char* ScreenDetailTxIsp_BackLabel()
{
    return !MainRuntime_IsTxIspActive() ? "Back" : (confirmExit ? "Cancel" : "Finish");
}
