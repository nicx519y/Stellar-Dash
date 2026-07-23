#include "CONFIG.h"
#include "HAL.h"

#include "board_latest_ch585.h"
#include "board_role_selector.h"

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

#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
    PWR_DCDCCfg(ENABLE);
#endif
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(SYSCLK_FREQ);
    rfm_board_latest_ch585_set_usb_spi_owner(false);
    rfm_board_latest_ch585_prepare_spi_pins();

    if(!rfm_board_role_selector_wait(&role))
    {
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
        (void)usb_subsystem_run(role);
    }

    board_safe_idle();
    return 0;
}
