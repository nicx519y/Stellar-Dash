#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "rotary-encoder.h"

GPIO_TypeDef hbox_test_gpio_a = {1u};
GPIO_TypeDef hbox_test_gpio_h = {2u};

static uint32_t test_tick;
static uint32_t test_primask;
static uint8_t test_ab;
static bool test_button_down;

uint32_t HAL_GetTick(void)
{
    return test_tick;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == GPIOH && pin == GPIO_PIN_8) {
        return (test_ab & 2u) != 0u ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    if (port == GPIOH && pin == GPIO_PIN_9) {
        return (test_ab & 1u) != 0u ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    if (port == GPIOA && pin == GPIO_PIN_0) {
        return test_button_down ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
    return GPIO_PIN_RESET;
}

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    (void)port;
    (void)init;
}

uint32_t hbox_test_get_primask(void)
{
    return test_primask;
}

void hbox_test_disable_irq(void)
{
    test_primask = 1u;
}

void hbox_test_enable_irq(void)
{
    test_primask = 0u;
}

static void sample_for(uint32_t milliseconds)
{
    for (uint32_t i = 0u; i < milliseconds; ++i) {
        ++test_tick;
        RotEnc_Tick1msFromISR();
    }
}

static void set_ab(uint8_t ab)
{
    test_ab = ab;
    sample_for(1u);
}

static void reset_fixture(void)
{
    test_tick = 0u;
    test_primask = 0u;
    test_ab = 0u;
    test_button_down = false;
    RotEnc_Init();
    sample_for(1001u);
    (void)RotEnc_GetDelta();
    (void)RotEnc_GetDetentDelta();
    (void)RotEnc_WasButtonPressed();
    (void)RotEnc_WasButtonReleased();
    (void)RotEnc_WasButtonClicked();
    (void)RotEnc_WasButtonLongPressed();
}

static void test_rotation_survives_main_loop_stall(void)
{
    reset_fixture();

    /* No RotEnc_Update() calls: SysTick alone must retain both detents. */
    set_ab(2u);
    set_ab(3u);
    set_ab(1u);
    set_ab(0u);

    assert(RotEnc_GetDelta() == 4);
    assert(RotEnc_GetDetentDelta() == 2);

    set_ab(1u);
    set_ab(3u);
    set_ab(2u);
    set_ab(0u);
    assert(RotEnc_GetDelta() == -4);
    assert(RotEnc_GetDetentDelta() == -2);
}

static void test_invalid_jump_does_not_create_detent(void)
{
    reset_fixture();
    set_ab(3u); /* 00 -> 11 changes both bits and must reset partial state. */
    set_ab(1u);
    assert(RotEnc_GetDelta() == 1);
    assert(RotEnc_GetDetentDelta() == 0);
}

static void test_button_requires_ten_stable_samples(void)
{
    reset_fixture();
    test_button_down = true;
    sample_for(9u);
    assert(!RotEnc_IsButtonDown());
    assert(!RotEnc_WasButtonPressed());

    sample_for(1u);
    assert(RotEnc_IsButtonDown());
    assert(RotEnc_WasButtonPressed());

    test_button_down = false;
    sample_for(9u);
    assert(!RotEnc_WasButtonClicked());
    sample_for(1u);
    assert(!RotEnc_IsButtonDown());
    assert(RotEnc_WasButtonReleased());
    assert(RotEnc_WasButtonClicked());
}

static void test_long_press_is_not_also_a_click(void)
{
    reset_fixture();
    test_button_down = true;
    sample_for(10u);
    assert(RotEnc_WasButtonPressed());
    sample_for(1000u);
    assert(RotEnc_WasButtonLongPressed());

    test_button_down = false;
    sample_for(10u);
    assert(RotEnc_WasButtonReleased());
    assert(!RotEnc_WasButtonClicked());
}

int main(void)
{
    test_rotation_survives_main_loop_stall();
    test_invalid_jump_does_not_create_detent();
    test_button_requires_ten_stable_samples();
    test_long_press_is_not_also_a_click();
    puts("rotary encoder tests passed");
    return 0;
}
