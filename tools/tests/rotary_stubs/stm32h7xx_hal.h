#ifndef HBOX_ROTARY_TEST_STM32H7XX_HAL_H
#define HBOX_ROTARY_TEST_STM32H7XX_HAL_H

#include <stdint.h>

typedef struct GPIO_TypeDef {
    uint8_t id;
} GPIO_TypeDef;

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET = 1,
} GPIO_PinState;

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

extern GPIO_TypeDef hbox_test_gpio_a;
extern GPIO_TypeDef hbox_test_gpio_h;

#define GPIOA (&hbox_test_gpio_a)
#define GPIOH (&hbox_test_gpio_h)

#define GPIO_PIN_0 (1u << 0)
#define GPIO_PIN_8 (1u << 8)
#define GPIO_PIN_9 (1u << 9)

#define GPIO_MODE_INPUT 0u
#define GPIO_PULLUP 1u
#define GPIO_SPEED_FREQ_LOW 0u

#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOH_CLK_ENABLE() ((void)0)

uint32_t HAL_GetTick(void);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);

uint32_t hbox_test_get_primask(void);
void hbox_test_disable_irq(void);
void hbox_test_enable_irq(void);

#define __get_PRIMASK() hbox_test_get_primask()
#define __disable_irq() hbox_test_disable_irq()
#define __enable_irq() hbox_test_enable_irq()
#define __DMB() ((void)0)

#endif
