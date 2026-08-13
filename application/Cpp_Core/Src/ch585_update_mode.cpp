#include "ch585_update_mode.hpp"

#include "board_cfg.h"
#include "board_power.hpp"
#include "ch585_role_bootstrap.hpp"
#include "ch585_iap_client.hpp"
#include "ch585_firmware_update.hpp"
#include "rf_bridge_port.hpp"
#include "storagemanager.hpp"
#include "system_logger.h"
#include "usb_board_link.hpp"
#include "usbdriver.hpp"

namespace {

static bool serviceFlagSet(uint8_t bit)
{
    return (STORAGE_MANAGER.config.screenControl.serviceFlags & bit) != 0u;
}

static bool persistServiceFlag(uint8_t bit, bool enabled)
{
    uint8_t& flags = STORAGE_MANAGER.config.screenControl.serviceFlags;
    const uint8_t previous = flags;
    flags = enabled ? static_cast<uint8_t>(flags | bit)
                    : static_cast<uint8_t>(flags & ~bit);
    if (flags == previous) return true;
    if (STORAGE_MANAGER.saveConfig()) return true;
    flags = previous;
    return false;
}

} // namespace

bool Ch585UpdateMode::isManualIspActive() const
{
    /* Fail safe during initial IAP migration and after an interrupted/failed
     * SPI update.  Until a live IAP plus Application/CAPS verification has
     * succeeded, STM32 must never negotiate a CH585 role: doing so steals the
     * USB pins from the ROM ISP shortly after BOOT-held power-up. */
    return serviceFlagSet(SCREEN_SERVICE_CH585_MANUAL_ISP_ACTIVE) ||
           !isIapConfirmed() || CH585_FIRMWARE_UPDATE.hasFailed();
}

bool Ch585UpdateMode::isManualIspPowered() const
{
    return isManualIspActive() && manualIspPowered;
}

bool Ch585UpdateMode::isIapConfirmed() const
{
    /* A completed bridge transaction includes IAP PROBE, full Application
     * CRC, maintenance role and CAPS verification. APPLIED is therefore an
     * authoritative IAP confirmation even before the legacy config bit is
     * persisted. An explicit USB ISP request remains independent. */
    return serviceFlagSet(SCREEN_SERVICE_CH585_IAP_CONFIRMED) ||
           CH585_FIRMWARE_UPDATE.hasAppliedImage();
}

bool Ch585UpdateMode::isManualEntryVisible() const
{
#if CH585_MANUAL_ISP_ENTRY_ENABLE
    return !isIapConfirmed() || isManualIspActive() ||
           CH585_FIRMWARE_UPDATE.isPending() || CH585_FIRMWARE_UPDATE.hasFailed();
#else
    return isManualIspActive() || CH585_FIRMWARE_UPDATE.isPending() ||
           CH585_FIRMWARE_UPDATE.hasFailed();
#endif
}

bool Ch585UpdateMode::requestManualIsp()
{
    return persistServiceFlag(SCREEN_SERVICE_CH585_MANUAL_ISP_ACTIVE, true);
}

bool Ch585UpdateMode::requestExitManualIsp()
{
    if (!CH585_IAP_CLIENT.probe()) {
        APP_STAGE_ERROR("M02", "CH585 IAP capability probe failed; manual ISP mode retained");
        return false;
    }
    if (!CH585_IAP_CLIENT.validateApplication()) {
        APP_STAGE_ERROR("M02A", "CH585 Application/CAPS verification failed; manual ISP mode retained");
        return false;
    }
    if (!CH585_FIRMWARE_UPDATE.acknowledgeManualRecovery()) {
        APP_STAGE_ERROR("M02J", "CH585 recovery verified but stale failure journal could not be cleared");
        return false;
    }

    uint8_t& flags = STORAGE_MANAGER.config.screenControl.serviceFlags;
    const uint8_t previous = flags;
    flags = static_cast<uint8_t>(
        (flags | SCREEN_SERVICE_CH585_IAP_CONFIRMED) &
        ~SCREEN_SERVICE_CH585_MANUAL_ISP_ACTIVE);
    if (STORAGE_MANAGER.saveConfig()) {
        APP_STAGE("M03", "CH585 IAP and Application/CAPS confirmed; temporary manual entry hidden");
        return true;
    }
    flags = previous;
    return false;
}

bool Ch585UpdateMode::setIapConfirmed(bool confirmed)
{
    return persistServiceFlag(SCREEN_SERVICE_CH585_IAP_CONFIRMED, confirmed);
}

void Ch585UpdateMode::setupManualIspRuntime()
{
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
    BOARD_POWER.enterRecoveryUiState();
    BOARD_POWER.setCh585Enabled(false);
    HAL_Delay(CH585_POWER_OFF_MIN_MS);
    BOARD_POWER.setCh585Enabled(true);
    manualIspPowered = true;
    APP_STAGE("M01", "CH585 manual ISP powered; SPI role and USB host takeover suppressed");
}

void Ch585UpdateMode::shutdownManualIspRuntime()
{
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
    BOARD_POWER.setCh585Enabled(false);
    manualIspPowered = false;
    APP_STAGE("M01X", "exited isolated CH585 USB ISP state");
}

bool Ch585UpdateMode::powerOnManualIsp()
{
    if (!isManualIspActive()) return false;

    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
    BOARD_POWER.enterRecoveryUiState();
    BOARD_POWER.setCh585Enabled(false);
    HAL_Delay(CH585_POWER_OFF_MIN_MS);
    BOARD_POWER.setCh585Enabled(true);
    manualIspPowered = true;
    APP_STAGE("M04", "CH585 powered for USB ISP; STM32 role takeover remains suppressed");
    return true;
}

void Ch585UpdateMode::powerOffManualIsp()
{
    if (!isManualIspActive()) return;

    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
    BOARD_POWER.setCh585Enabled(false);
    manualIspPowered = false;
    APP_STAGE("M05", "CH585 USB ISP retry requested; CH585 held off");
}
