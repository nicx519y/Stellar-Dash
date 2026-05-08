#include "usbd_compatibility_hid.h"

__attribute__((aligned(4))) uint8_t HID_Report_Buffer[DEF_USBD_HS_PACK_SIZE];
volatile uint8_t HID_Set_Report_Flag = SET_REPORT_DEAL_OVER;
volatile uint16_t Data_Pack_Max_Len = 0;
volatile uint16_t Head_Pack_Len = 0;
volatile uint16_t Uart_Input_Ptr = 0;
volatile uint16_t Uart_RecLen = 0;
volatile uint16_t USB_RecLen = 0;
volatile uint8_t UploadPoint_Busy = 0;
volatile uint16_t Uart_Timeout_Count = 0;
