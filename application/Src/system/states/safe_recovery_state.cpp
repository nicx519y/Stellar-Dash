#include "states/safe_recovery_state.hpp"

#include "board_cfg.h"
#include "board_power.hpp"
#include "ch585_role_bootstrap.hpp"
#include "rf_bridge_port.hpp"
#include "usb_board_link.hpp"
#include "usbdriver.hpp"
#include "system_logger.h"
#include "release_installer.hpp"
#include "stm32h7xx_hal.h"
#include "screen_control/spi_screen_manager.hpp"

bool SafeRecoveryState::enter()
{
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
    BOARD_POWER.enterRecoveryUiState();
    BOARD_POWER.setCh585Enabled(false);
    if(RELEASE_INSTALLER.failed())SPIScreenManager::getInstance().showFirmwareRecovery();
    APP_STAGE_ERROR("A13R", "entered safe recovery state; CH585 transports disabled");
    return true;
}

void SafeRecoveryState::tick()
{
    // Local recovery is independent of the missing TX maintenance channel.
    // Require release before a deliberate two-second physical GPIO1+FN hold.
    static bool armed=false;
    static uint32_t held=0;
    const bool a=HAL_GPIO_ReadPin(GPIO_BTN1_PORT,GPIO_BTN1_PIN)==GPIO_PIN_RESET;
    const bool b=HAL_GPIO_ReadPin(GPIO_BTN4_PORT,GPIO_BTN4_PIN)==GPIO_PIN_RESET;
    if(!a && !b){armed=true;held=0;}
    else if(armed && a && b && RELEASE_INSTALLER.failed()) {
        if(!held)held=HAL_GetTick();
        if(HAL_GetTick()-held>=2000){armed=false;held=0;(void)RELEASE_INSTALLER.retry();}
    } else held=0;
}

void SafeRecoveryState::exit()
{
}
