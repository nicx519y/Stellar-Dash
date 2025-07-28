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
    {GPIO_BTN1_PORT, GPIO_BTN1_PIN, GPIO_BTN1_VIRTUAL_PIN},
    {GPIO_BTN2_PORT, GPIO_BTN2_PIN, GPIO_BTN2_VIRTUAL_PIN},
    {GPIO_BTN3_PORT, GPIO_BTN3_PIN, GPIO_BTN3_VIRTUAL_PIN},
    {GPIO_BTN4_PORT, GPIO_BTN4_PIN, GPIO_BTN4_VIRTUAL_PIN}
};



void GPIO_Btns_Init(void);
void GPIO_Btns_Iterate( void (*callback)(uint8_t virtualPin, bool isPressed, uint8_t idx) );

#ifdef __cplusplus
}
#endif

#endif // !__GPIO_BTN_H__
