#include "ch585_usbhs_device.h"

/*
 * Minimal symbol bridge required by ch585_usbhs_device.c
 * for class request handling.
 */
volatile uint8_t HID_Set_Report_Flag = 0u;
__attribute__((aligned(4))) uint8_t HID_Report_Buffer[DEF_USBD_HS_PACK_SIZE] = {0};
volatile uint16_t USB_RecLen = 0u;
volatile uint8_t UploadPoint_Busy = 0u;
volatile uint16_t Data_Pack_Max_Len = 0u;
volatile uint16_t Head_Pack_Len = 0u;
