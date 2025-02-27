/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021, Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

#ifndef BOARD_H_
#define BOARD_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "stm32h7xx_hal_rcc.h"

#define SYSTEM_CLOCK_FREQ      480000000

#define __FPU_PRESENT          1 // 启用FPU
#define __FPU_USED             1 // 启用FPU
#define FPU_FPDSCR_RMode_Msk   (0x3 << 22) // 清除舍入模式位 [23:22]
#define FPU_FPDSCR_RMode_RN    (0x0 << 22) // 舍入模式为 Round to Nearest (RN)

/* Debug print configuration */
#define APPLICATION_DEBUG_PRINT  1   // 设置为 0 可以关闭所有调试打印

#if APPLICATION_DEBUG_PRINT
    #define APP_DBG(fmt, ...) printf("[APP] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define APP_DBG(fmt, ...) ((void)0)
#endif

#if APPLICATION_DEBUG_PRINT
    #define APP_ERR(fmt, ...) printf("[APP][ERROR] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define APP_ERR(fmt, ...) ((void)0)
#endif

static inline void board_stm32h7_post_init(void)
{
  // For this board does nothing
}

#ifdef __cplusplus
 }
#endif

#endif
