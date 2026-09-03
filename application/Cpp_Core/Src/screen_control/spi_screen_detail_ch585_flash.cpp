#include "screen_control/spi_screen_detail_entries.hpp"

#include "ch585_update_mode.hpp"
#include "ch585_iap_client.hpp"
#include "ch585_firmware_update.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "stm32h7xx.h"
#include "main_runtime_control.hpp"

uint8_t ScreenDetailCh585Flash_InitIndex(void)
{
    return 0u;
}

void ScreenDetailCh585Flash_Rotate(uint8_t* ioIndex, int8_t det)
{
    (void)ioIndex;
    (void)det;
}

void ScreenDetailCh585Flash_Render(ST7789_Handle* lcd,
                                   uint8_t index,
                                   const ScreenUiStyle& style)
{
    (void)index;
    static const char* const enterLines[] = {
        "Temporary CH585 ISP entry.",
        "Press Enter to hold CH585 off.",
        "Then use BOOT and Power for ISP.",
        "Use WCHISPStudio; no flash locks."
    };
    static const char* const readyLines[] = {
        "CH585 power is OFF.",
        "Hold the physical BOOT button.",
        "Press Power while holding BOOT.",
        "Keep holding until WCH finds it."
    };
    static const char* const poweredLines[] = {
        "CH585 is powered in manual ISP.",
        "Flash the combined image in WCH.",
        "Release BOOT after flash succeeds.",
        "Press Verify for IAP + App CAPS."
    };
    static const char* const probeFailedLines[] = {
        "IAP was not detected over SPI.",
        "Manual ISP mode is still active.",
        "Press Back to power off and retry.",
        "No protection or lock was changed."
    };
    static const char* const updatePendingLines[] = {
        "CH585 firmware is staged.",
        "The local SPI update is starting.",
        "Do not disconnect device power.",
        "The IAP loader remains recoverable."
    };
    static const char* const updateFailedLines[] = {
        "CH585 SPI firmware update failed.",
        "The staged firmware is preserved.",
        "Press Retry to run the update again.",
        "No protection or lock was changed."
    };
    const bool active = CH585_UPDATE_MODE.isManualIspActive();
    const bool powered = CH585_UPDATE_MODE.isManualIspPowered();
    const bool probeFailed =
        active && powered &&
        CH585_IAP_CLIENT.status() != Ch585IapClientStatus::Idle &&
        CH585_IAP_CLIENT.status() != Ch585IapClientStatus::Ready;
    const char* const* lines = probeFailed ? probeFailedLines
        : (CH585_FIRMWARE_UPDATE.isPending() ? updatePendingLines
        : (active ? (powered ? poweredLines : readyLines)
        : (CH585_FIRMWARE_UPDATE.hasFailed() ? updateFailedLines : enterLines)));
    ScreenDetailRender_TitleLines(lcd,
                                  "CH585 Flash",
                                  lines,
                                  4u,
                                  style);
}

bool ScreenDetailCh585Flash_OnConfirm(uint8_t index)
{
    (void)index;
    if (CH585_FIRMWARE_UPDATE.isPending()) return false;
    const bool active = CH585_UPDATE_MODE.isManualIspActive();
    if (!active) {
        if (CH585_FIRMWARE_UPDATE.hasFailed()) {
            return CH585_FIRMWARE_UPDATE.requestRetry();
        }
        if (!CH585_UPDATE_MODE.requestManualIsp()) return false;
        MainRuntime_RequestReset();
        return false;
    }
    if (!CH585_UPDATE_MODE.isManualIspPowered()) {
        return CH585_UPDATE_MODE.powerOnManualIsp();
    }
    if (!CH585_UPDATE_MODE.requestExitManualIsp()) return false;
    MainRuntime_RequestReset();
    return false;
}

bool ScreenDetailCh585Flash_OnBack(void)
{
    if (!CH585_UPDATE_MODE.isManualIspActive()) return true;
    if (CH585_UPDATE_MODE.isManualIspPowered()) {
        CH585_UPDATE_MODE.powerOffManualIsp();
    }
    return false;
}

const char* ScreenDetailCh585Flash_ConfirmLabel(void)
{
    if (CH585_FIRMWARE_UPDATE.isPending()) return "Wait";
    if (!CH585_UPDATE_MODE.isManualIspActive()) {
        return CH585_FIRMWARE_UPDATE.hasFailed() ? "Retry" : "Enter";
    }
    return CH585_UPDATE_MODE.isManualIspPowered() ? "Verify" : "Power";
}
