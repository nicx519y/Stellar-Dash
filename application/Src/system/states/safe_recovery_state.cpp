#include "states/safe_recovery_state.hpp"

#include "board_cfg.h"
#include "board_power.hpp"
#include "ch585_role_bootstrap.hpp"
#include "rf_bridge_port.hpp"
#include "usb_board_link.hpp"
#include "usbdriver.hpp"
#include "system_logger.h"

bool SafeRecoveryState::enter()
{
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
    BOARD_POWER.enterRecoveryUiState();
    BOARD_POWER.setCh585Enabled(false);
    APP_STAGE_ERROR("A13R", "entered safe recovery state; CH585 transports disabled");
    return true;
}

void SafeRecoveryState::tick()
{
}

void SafeRecoveryState::exit()
{
}
