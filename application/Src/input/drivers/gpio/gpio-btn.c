#include "gpio-btn.h"




void GPIO_Btns_Init()
{
    // 时钟已在gpio.c种使能
    GPIO_InitTypeDef GPIO_Init;

    GPIO_Init.Mode = GPIO_MODE_INPUT;
    GPIO_Init.Pull = GPIO_PULLUP;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    for(uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        GPIO_Init.Pin = gpio_btns_mapping[i].pin;
        HAL_GPIO_Init(gpio_btns_mapping[i].port, &GPIO_Init);
    }
}

void GPIO_Btns_Iterate( void (*callback)(uint8_t virtualPin, bool isPressed, uint8_t idx) ) {
    for(uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        callback(gpio_btns_mapping[i].virtualPin, 
                HAL_GPIO_ReadPin(gpio_btns_mapping[i].port, gpio_btns_mapping[i].pin) == GPIO_PIN_RESET, 
                i);
    }
}



