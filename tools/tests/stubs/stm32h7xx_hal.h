#ifndef HBOX_TEST_STM32H7XX_HAL_H
#define HBOX_TEST_STM32H7XX_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_TEST_STM32H7XX_HAL_H */
