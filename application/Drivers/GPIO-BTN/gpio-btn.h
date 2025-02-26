#ifndef __GPIO_BTN_H__
#define __GPIO_BTN_H__

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include "constant.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void GPIO_Btn_Init(void);
bool GPIO_Btn_IsPressed(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif // !__GPIO_BTN_H__
