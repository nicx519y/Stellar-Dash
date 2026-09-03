#include "states/ch585_usb_isp_state.hpp"

#include "board_cfg.h"
#include "ch585_update_mode.hpp"
#include "system_logger.h"

bool Ch585UsbIspState::enter()
{
    /* This is the proven USB-ROM recovery path.  It deliberately owns no
     * CH585 SPI role and never starts the STM32 USB host/link runtime. */
    CH585_UPDATE_MODE.setupManualIspRuntime();
    APP_STAGE("M01S", "entered isolated CH585 USB ISP state");
    return true;
}

void Ch585UsbIspState::tick()
{
    /* Power/Verify/Back remain owned by the existing screen detail handlers. */
}

void Ch585UsbIspState::exit()
{
    CH585_UPDATE_MODE.shutdownManualIspRuntime();
}
