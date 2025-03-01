#include "gpio-btn.h"




void GPIO_Btn_Init()
{
    // 时钟已在gpio.c种使能
    GPIO_InitTypeDef GPIO_Init;

    GPIO_Init.Mode = GPIO_MODE_INPUT;
    GPIO_Init.Pull = GPIO_PULLDOWN;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_HIGH;

    for(uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        GPIO_Init.Pin = gpio_btns_mapping[i].pin;
        HAL_GPIO_Init(gpio_btns_mapping[i].port, &GPIO_Init);
    }
}

bool GPIO_Btn_IsPressed(uint8_t virtualPin) {
    for(uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        if(gpio_btns_mapping[i].virtualPin == virtualPin) {
            return HAL_GPIO_ReadPin(gpio_btns_mapping[i].port, gpio_btns_mapping[i].pin) == GPIO_PIN_RESET;
        }
    }
    return false;
}

