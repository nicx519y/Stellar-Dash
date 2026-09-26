#include "usbd_compatibility_hid.h"

__attribute__((aligned(4))) uint8_t UART2_Rx_Buf[UART_REV_BUFFLEN];
uint8_t HID_Report_Buffer[DEF_USBD_HS_PACK_SIZE];
volatile uint8_t HID_Set_Report_Flag;
volatile uint16_t Data_Pack_Max_Len;
volatile uint16_t Head_Pack_Len;
volatile uint16_t Uart_Input_Ptr;
volatile uint16_t Uart_RecLen;
volatile uint16_t USB_RecLen;
volatile uint8_t UploadPoint_Busy;
volatile uint16_t Uart_Timeout_Count;

void UART2_Tx_Service(void) {}
void UART2_Rx_Service(void) {}
void USART2_Init(uint32_t baudrate) { (void)baudrate; }
void UART2_DMA_Init(void) {}
void TIM2_Init(void) {}
void HID_Set_Report_Deal(void) {}
