#include "boot_profile.h"
#include "ch585_role_bootstrap.hpp"

#include "board_cfg.h"
#include "board_power.hpp"
#include "rf_boot_ready.hpp"
#include "stm32h7xx_hal.h"
#include "system_logger.h"
#include "usb_board_link_port.hpp"

void Ch585RoleBootstrap::setSelector(Ch585RoleSelector selectorFn)
{
    selector = selectorFn;
}

void Ch585RoleBootstrap::shutdown()
{
    usbStartupPrepared = false;
    (void)BOARD_POWER.setUsbHostEnabled(false);
    BOARD_POWER.setCh585Enabled(false);
    RFBootReady::reset();
    activeRole = Ch585Role::SafeIdle;
    bootstrapState = Ch585BootstrapState::Off;
}

bool Ch585RoleBootstrap::prepareUsbStartup()
{
    // Only the initial Off state may be prepared. Never disturb an active role.
    if (usbStartupPrepared) return true;
    if (bootstrapState != Ch585BootstrapState::Off ||
        !BOARD_POWER.isInitialized() || BOARD_POWER.isCh585Enabled()) return false;

    shutdown();
    HAL_Delay(CH585_POWER_OFF_MIN_MS);
    // Set NSS inactive while CH585 is still off. No SELECT_ROLE/CAPS traffic.
    if (!USBBoardLinkPort_Init()) return false;
    bootstrapState = Ch585BootstrapState::Booting;
    BOARD_POWER.setCh585Enabled(true);
    usbPowerOnAtMs = HAL_GetTick();
    usbStartupPrepared = true;
    return true;
}

bool Ch585RoleBootstrap::selectOnce(Ch585Role requestedRole)
{
    const uint32_t startMs = HAL_GetTick();
    bootstrapState = Ch585BootstrapState::Selecting;

    do {
        if (selector(requestedRole)) {
            activeRole = requestedRole;
            bootstrapState = Ch585BootstrapState::Locked;
            return true;
        }
        HAL_Delay(CH585_ROLE_SELECT_RETRY_MS);
    } while ((uint32_t)(HAL_GetTick() - startMs) < CH585_ROLE_SELECT_TIMEOUT_MS);

    return false;
}

bool Ch585RoleBootstrap::start(Ch585Role requestedRole)
{
    BP_APP_SCOPE(BP_APP_CH585_START);
    if (requestedRole == Ch585Role::SafeIdle || selector == nullptr) {
        shutdown();
        bootstrapState = Ch585BootstrapState::Failed;
        return false;
    }

    if (isLocked() && activeRole == requestedRole) {
        return true;
    }

    /* An unusually slow UI/power probe may outlive the CH585 selector window.
     * Reuse only within the original power-settle + ready-hint budget;
     * otherwise take the unchanged cold-start path, including its retry. */
    bool reusePrepared = usbStartupPrepared && requestedRole == Ch585Role::Usb &&
        bootstrapState == Ch585BootstrapState::Booting && BOARD_POWER.isCh585Enabled() &&
        (uint32_t)(HAL_GetTick() - usbPowerOnAtMs) <
            CH585_POWER_ON_SETTLE_MS + CH585_READY_HINT_TIMEOUT_MS;
    usbStartupPrepared = false; // one-shot, including all failure paths

    /* Initial attempt plus exactly one power-cycle retry. */
    for (uint8_t attempt = 0u; attempt < 2u; ++attempt) {
        APP_STAGE("R01", "CH585 role bootstrap attempt=%u role=%u",
                  static_cast<unsigned int>(attempt + 1u),
                  static_cast<unsigned int>(requestedRole));
        uint32_t readyTimeoutMs = CH585_READY_HINT_TIMEOUT_MS;
        if (reusePrepared) {
            uint32_t elapsed = HAL_GetTick() - usbPowerOnAtMs;
            if (elapsed < CH585_POWER_ON_SETTLE_MS) {
                HAL_Delay(CH585_POWER_ON_SETTLE_MS - elapsed);
            }
            elapsed = HAL_GetTick() - usbPowerOnAtMs;
            const uint32_t deadline = CH585_POWER_ON_SETTLE_MS + CH585_READY_HINT_TIMEOUT_MS;
            readyTimeoutMs = elapsed < deadline ? deadline - elapsed : 0u;
            reusePrepared = false;
        } else {
            shutdown();
            HAL_Delay(CH585_POWER_OFF_MIN_MS);
            bootstrapState = Ch585BootstrapState::Booting;
            BOARD_POWER.setCh585Enabled(true);
            /* Do not clock SELECT_ROLE on the power-up edge. The original
             * cold start and power-cycle retry retain their full settle. */
            HAL_Delay(CH585_POWER_ON_SETTLE_MS);
        }

        /* Keep SPI completely idle while the persistent IAP observes its
         * 500-ms boot window.  Jumping from inside an active NSS transaction
         * leaves SPI0 in an ambiguous hand-off state; the loader's idle boot
         * path reaches the application with a clean bus instead. */
        APP_STAGE("R02", "CH585 waiting for idle IAP-to-application handoff");
        const bool readyObserved =
            BP_APP_CALL(BP_APP_CH585_READY,
                RFBootReady::waitForModuleReady(readyTimeoutMs));
        if (!readyObserved) {
            /*
             * PA5 is shared with the steady-state event signal and its short
             * boot pulse is only a timing hint. Some power cycles do not
             * expose that pulse to STM32 even though the CH585 polling role
             * selector is already alive. Continue with the bounded protocol
             * transaction; ROLE_SELECTED remains the authoritative commit.
             */
            APP_STAGE("R02B",
                      "CH585 ready pulse not observed; probing role selector");
        } else {
            APP_STAGE("R02A", "CH585 application-ready pulse observed");
        }

        if (BP_APP_CALL(BP_APP_CH585_SELECT, selectOnce(requestedRole))) {
            APP_STAGE("R03", "CH585 role selected: attempt=%u role=%u",
                      static_cast<unsigned int>(attempt + 1u),
                      static_cast<unsigned int>(requestedRole));
            return true;
        }
        APP_STAGE_ERROR("R03E", "CH585 role select window expired: attempt=%u state=%u",
                        static_cast<unsigned int>(attempt + 1u),
                        static_cast<unsigned int>(bootstrapState));
    }

    shutdown();
    bootstrapState = Ch585BootstrapState::Failed;
    return false;
}
