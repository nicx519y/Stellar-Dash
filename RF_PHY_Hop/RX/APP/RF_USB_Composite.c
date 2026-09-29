#include "CONFIG.h"
#include "ch585_usbhs_device.h"

void RF_USB_CompositeInit(void)
{
    /* USB2 controller on PB12(D-) / PB13(D+) is configured in USBHS_Device_Init. */
    USBHS_Device_Init(ENABLE);
}
