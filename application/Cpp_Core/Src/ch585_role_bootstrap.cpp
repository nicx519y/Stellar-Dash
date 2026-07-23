#include "ch585_role_bootstrap.hpp"

#include "board_cfg.h"
#include "board_power.hpp"
#include "rf_boot_ready.hpp"
#include "stm32h7xx_hal.h"

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
        shutdown();
        HAL_Delay(CH585_POWER_OFF_MIN_MS);

        bootstrapState = Ch585BootstrapState::Booting;
        BOARD_POWER.setCh585Enabled(true);

        if (selectOnce(requestedRole)) {
            return true;
        }
    }

    shutdown();
    bootstrapState = Ch585BootstrapState::Failed;
    return false;
}
