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
    (void)BOARD_POWER.setUsbHostEnabled(false);
    BOARD_POWER.setCh585Enabled(false);
    RFBootReady::reset();
    activeRole = Ch585Role::SafeIdle;
    bootstrapState = Ch585BootstrapState::Off;
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
    if (requestedRole == Ch585Role::SafeIdle || selector == nullptr) {
        shutdown();
        bootstrapState = Ch585BootstrapState::Failed;
        return false;
    }

    if (isLocked() && activeRole == requestedRole) {
        return true;
    }

    /* Initial attempt plus exactly one power-cycle retry. */
    for (uint8_t attempt = 0u; attempt < 2u; ++attempt) {
        APP_STAGE("R01", "CH585 role bootstrap attempt=%u role=%u",
                  static_cast<unsigned int>(attempt + 1u),
                  static_cast<unsigned int>(requestedRole));
        shutdown();
        HAL_Delay(CH585_POWER_OFF_MIN_MS);

        bootstrapState = Ch585BootstrapState::Booting;
        BOARD_POWER.setCh585Enabled(true);
        /*
         * Do not clock SELECT_ROLE on the CH585 power-up edge.  The IAP path
         * already observes the same settle interval; the application needs
         * time to finish its reset/startup code and arm the cold-boot SPI
         * selector before the first five-byte transaction arrives.
         */
        HAL_Delay(CH585_POWER_ON_SETTLE_MS);

        /* Keep SPI completely idle while the persistent IAP observes its
         * 500-ms boot window.  Jumping from inside an active NSS transaction
         * leaves SPI0 in an ambiguous hand-off state; the loader's idle boot
         * path reaches the application with a clean bus instead. */
        APP_STAGE("R02", "CH585 waiting for idle IAP-to-application handoff");
        if (!RFBootReady::waitForModuleReady(CH585_ROLE_SELECT_TIMEOUT_MS)) {
            APP_STAGE_ERROR("R02E", "CH585 application-ready pulse not observed");
            continue;
        }
        APP_STAGE("R02A", "CH585 application-ready pulse observed");

        if (selectOnce(requestedRole)) {
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
