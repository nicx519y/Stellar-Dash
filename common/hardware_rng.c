#include "hardware_rng.h"

#include <string.h>

#include "stm32h7xx_hal.h"

#define HBOX_RNG_READY_SPINS 1000000u

int HBoxHardwareRng_Init(void)
{
    uint32_t spins = HBOX_RNG_READY_SPINS;

    SET_BIT(RCC->CR, RCC_CR_HSI48ON);
    while ((READ_BIT(RCC->CR, RCC_CR_HSI48RDY) == 0u) &&
           (spins-- != 0u)) {
    }
    if (READ_BIT(RCC->CR, RCC_CR_HSI48RDY) == 0u) {
        return 0;
    }

    /*
     * RNGSEL=0 selects HSI48 on STM32H750.  Keep the write explicit so a
     * previous boot stage cannot leave the RNG driven above its limit.
     */
    CLEAR_BIT(RCC->D2CCIP2R, RCC_D2CCIP2R_RNGSEL);
    SET_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);
    (void)READ_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);
    RNG->CR = RNG_CR_RNGEN;
    return 1;
}

int HBoxHardwareRng_Fill(void *context,
                         unsigned char *output,
                         size_t length)
{
    size_t offset = 0u;
    (void)context;

    if ((output == NULL && length != 0u) ||
        (READ_BIT(RNG->CR, RNG_CR_RNGEN) == 0u)) {
        return -1;
    }
    while (offset < length) {
        uint32_t spins = HBOX_RNG_READY_SPINS;
        uint32_t value;
        size_t copy_length;

        while ((READ_BIT(RNG->SR, RNG_SR_DRDY) == 0u) &&
               (spins-- != 0u)) {
            if (READ_BIT(RNG->SR, RNG_SR_SECS | RNG_SR_CECS) != 0u) {
                RNG->CR = 0u;
                RNG->CR = RNG_CR_RNGEN;
            }
        }
        if (READ_BIT(RNG->SR, RNG_SR_DRDY) == 0u ||
            READ_BIT(RNG->SR, RNG_SR_SECS | RNG_SR_CECS) != 0u) {
            memset(output, 0, length);
            return -1;
        }
        value = RNG->DR;
        copy_length = (length - offset > sizeof(value))
                          ? sizeof(value)
                          : (length - offset);
        memcpy(&output[offset], &value, copy_length);
        offset += copy_length;
    }
    return 0;
}

void HBoxHardwareRng_Shutdown(void)
{
    RNG->CR = 0u;
    CLEAR_BIT(RCC->AHB2ENR, RCC_AHB2ENR_RNGEN);
}
