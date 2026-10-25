#include "CONFIG.h"
#include "HAL.h"

#include "board_latest_ch585.h"
#include "board_role_selector.h"
#include "fault_log_api.h"
#include "game_link_log.h"

int RF_FrozenMain(void);

/*
 * The independent USB implementation supplies a strong definition. Keeping a
 * weak safe fallback lets the RF board target remain buildable in isolation.
 */
__attribute__((weak))
int usb_subsystem_run(usb_board_role_t role)
{
    (void)role;
    return -1;
}

static void board_safe_idle(void)
{
    rfm_board_latest_ch585_set_usb_spi_owner(false);
    rfm_board_latest_ch585_stop_spi();
    rfm_board_latest_ch585_prepare_sleep_pins();
    for(;;)
    {
    }
}

int main(void)
{
    usb_board_role_t role = USB_BOARD_ROLE_NONE;
    XoraLog_Init();
    XoraLog_SaveFaultWindow(); // Drain retained evidence before transport starts.

#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
    PWR_DCDCCfg(ENABLE);
#endif
    /* The persistent 4 KiB IAP is the sole reset entry and establishes the
     * 60 MHz system clock before jumping here.  Re-running SetSysClock while
     * that PLL/clock tree is already active can stall before SPI role setup. */
    rfm_board_latest_ch585_set_usb_spi_owner(false);
    rfm_board_latest_ch585_prepare_spi_pins();

    if(!rfm_board_role_selector_wait(&role))
    {
        XoraLog_Event(FL_EVENT_ROLE,FL_CATEGORY_LINK,FL_ERROR,1);
        XoraLog_Service(1);
        board_safe_idle();
    }

    if(role == USB_BOARD_ROLE_RF)
    {
        rfm_board_latest_ch585_set_usb_spi_owner(false);
        return RF_FrozenMain();
    }
    if((role == USB_BOARD_ROLE_USB) ||
       (role == USB_BOARD_ROLE_MAINTENANCE))
    {
        rfm_board_latest_ch585_set_usb_spi_owner(true);
        const int result=usb_subsystem_run(role);
        XoraLog_Event(FL_EVENT_ROLE,FL_CATEGORY_LINK,FL_ERROR,(uint32_t)result);
        /* The failed subsystem may already own peripherals. Leave this in
         * RAM; do not assume its failure creates a safe Flash window. */
    }

    board_safe_idle();
    return 0;
}
