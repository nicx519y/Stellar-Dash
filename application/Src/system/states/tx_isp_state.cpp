#include "states/tx_isp_state.hpp"
#include "board_cfg.h"
#include "board_power.hpp"
#include "ch585_role_bootstrap.hpp"
#include "usb_board_link.hpp"
#include "usb_board_link_port.hpp"
#include "rf_bridge_port.hpp"
#include "system_sleep_manager.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "system_logger.h"
#include "stm32h7xx_hal.h"

bool TxIspState::enter()
{
    // The dispatcher has already stopped the previous input/WebConfig owner.
    SystemSleep_CancelForModeChange();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
    BOARD_POWER.enterRecoveryUiState();
    if (!USBBoardLinkPort_TryShutdown()) return false;

    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIO_InitTypeDef pins = {};
    pins.Pin = CH585_SPI_SCK_PIN | CH585_SPI_MOSI_PIN | CH585_SPI_MISO_PIN;
    pins.Mode = GPIO_MODE_ANALOG;
    pins.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(CH585_SPI_GPIO_PORT, &pins);
    // Drive NSS inactive before changing its mode; no ROM ISP SPI commands.
    HAL_GPIO_WritePin(CH585_SPI_GPIO_PORT, CH585_SPI_NSS_PIN, GPIO_PIN_SET);
    pins.Pin = CH585_SPI_NSS_PIN;
    pins.Mode = GPIO_MODE_OUTPUT_PP;
    pins.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CH585_SPI_GPIO_PORT, &pins);

    // PB22 must already be grounded when the user confirms Start on the screen.
    HAL_Delay(CH585_POWER_OFF_MIN_MS);
    BOARD_POWER.setCh585Enabled(true);
    SPIScreenManager::getInstance().showTxIsp();
    APP_STAGE("ISP1", "TX ROM ISP supply held on; SPI parked; manual exit only");
    return true;
}

void TxIspState::tick()
{
    // No SPI polling, role selection, power retry, reset or automatic exit.
    // The common runtime continues to service only local screen and power telemetry.
}

void TxIspState::exit()
{
    // Only a confirmed screen exit reaches this point. Remove PB22 first.
    BOARD_POWER.setCh585Enabled(false);
    APP_STAGE("ISP2", "TX ROM ISP state exited; normal runtime will select its role");
}
