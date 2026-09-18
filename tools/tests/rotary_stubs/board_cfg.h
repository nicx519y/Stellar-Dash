#ifndef HBOX_ROTARY_TEST_BOARD_CFG_H
#define HBOX_ROTARY_TEST_BOARD_CFG_H

#include "stm32h7xx_hal.h"

#define ROTENC_A_PORT GPIOH
#define ROTENC_A_PIN GPIO_PIN_8
#define ROTENC_B_PORT GPIOH
#define ROTENC_B_PIN GPIO_PIN_9
#define ROTENC_BTN_PORT GPIOA
#define ROTENC_BTN_PIN GPIO_PIN_0

#define APP_DBG(...) ((void)0)

#endif
