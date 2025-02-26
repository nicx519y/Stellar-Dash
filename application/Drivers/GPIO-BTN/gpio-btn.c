#include "gpio-btn.h"


void GPIO_Btn_Init()
{
    // 时钟已在gpio.c种使能
    GPIO_InitTypeDef GPIO_Init;

    GPIO_Init.Mode = GPIO_MODE_INPUT;
    GPIO_Init.Pull = GPIO_PULLDOWN;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_HIGH;

    for(uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        GPIO_Init.Pin = GPIO_BUTTONS_MAPPING[i].pin;
        HAL_GPIO_Init(GPIO_BUTTONS_MAPPING[i].port, &GPIO_Init);
    }
}

bool GPIO_Btn_IsPressed(uint8_t virtualPin) {
    for(uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        if(GPIO_BUTTONS_MAPPING[i].virtualPin == virtualPin) {
            return HAL_GPIO_ReadPin(GPIO_BUTTONS_MAPPING[i].port, GPIO_BUTTONS_MAPPING[i].pin) == GPIO_PIN_RESET;
        }
    }
    return false;
}


