#include "usb_management_control.h"
#include "rf_binding_platform.h"

void usb_management_control_hw_rf_binding(const uint8_t *request,uint8_t *response)
{
    /* Called from the USB/SPI main-loop parser. No BLE/RF initialization. */
    rfb_store_request(&rfb_flash,rfb_platform_identity(0u),0u,request,response);
}
