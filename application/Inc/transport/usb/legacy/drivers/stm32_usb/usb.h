#ifndef __USB_H_
#define __USB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

void USB_clock_init(void);
void USB_Device_Init(void);
void USB_Host_Init(void);



#ifdef __cplusplus
}
#endif

#endif // !__USB_H_