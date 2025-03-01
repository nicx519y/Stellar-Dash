#ifndef __GPIO_BTN_H__
#define __GPIO_BTN_H__

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include "board_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif


#include "main.h"


const struct gpio_pin_def gpio_btns_mapping[NUM_GPIO_BUTTONS] = {
    {GPIOC, GPIO_PIN_6, 17},
    {GPIOC, GPIO_PIN_7, 18},
    {GPIOC, GPIO_PIN_8, 19},
    {GPIOC, GPIO_PIN_9, 20}
};



void GPIO_Btn_Init(void);
bool GPIO_Btn_IsPressed(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif // !__GPIO_BTN_H__
