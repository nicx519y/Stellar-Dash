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

#ifndef __BOARD_H__
#define __BOARD_H__

#include "stm32h750xx.h"

#ifdef __cplusplus
 extern "C" {
#endif


#define SYSTEM_CLOCK_FREQ      480000000

#define SYSTEM_CHECK_ENABLE    0 // 使能缓存检查

#ifndef FPU_FPDSCR_RMode_Msk
    #define FPU_FPDSCR_RMode_Msk   (0x3 << 22) // 清除舍入模式位 [23:22]
#endif

#ifndef FPU_FPDSCR_RMode_RN
    #define FPU_FPDSCR_RMode_RN    (0x0 << 22) // 舍入模式为 Round to Nearest (RN)  
#endif

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

#define USB_DEBUG_PRINT 1

#if USB_DEBUG_PRINT
    #define USB_DBG(fmt, ...) printf("[USB] " fmt "\r\n", ##__VA_ARGS__)
    #define USB_ERR(fmt, ...) printf("[USB][ERROR] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define USB_DBG(fmt, ...) ((void)0)
    #define USB_ERR(fmt, ...) ((void)0)
#endif



#define FIRMWARE_VERSION                    (uint32_t)0x010000  //固件版本
#define CONFIG_VERSION                      (uint32_t)0x000002  //配置版本 三位版本号 0x aa bb cc
#define ADC_MAPPING_VERSION                 (uint32_t)0x000001  //ADC值映射表版本

#define WEB_RESOURCES_ADDR                  0x90200000       // 网页资源地址 memory map 地址 qspi flash 0x90100000 定义在 STM32H750XBHx_FLASH.ld 中
#define ADC_VALUES_MAPPING_ADDR             0x90300000  // ADC值映射表地址  qspi flash 0x00200000 定义在 STM32H750XBHx_FLASH.ld 中
#define CONFIG_ADDR                         0x90400000  // 配置数据地址  qspi flash 0x00300000 定义在 STM32H750XBHx_FLASH.ld 中

#define NUM_ADC_VALUES_MAPPING              8               // 最大8个映射 ADC按钮映射表用于值查找
#define MAX_ADC_VALUES_LENGTH               40              // 每个映射最大40个值 ADC按钮映射表用于值查找
#define MAX_NUM_MARKING_VALUE               100             // 每个step最大采集值个数
#define TIME_ADC_INIT                       1000            // ADC初始化时间，时间越长初始化越准确
#define NUM_WINDOW_SIZE                     8               // 校准滑动窗口大小

#define NUM_PROFILES                        16
#define NUM_ADC                             3               // 3个ADC
#define NUM_ADC1_BUTTONS                    6
#define NUM_ADC2_BUTTONS                    6
#define NUM_ADC3_BUTTONS                    5
#define NUM_ADC_BUTTONS                     (NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS + NUM_ADC3_BUTTONS)
#define MIN_ADC_TOP_DEADZONE                0.1             // 默认ADC顶部死区最小值
#define MIN_VALUE_DIFF_RATIO                0.8             // 最小值差值比例 按键动态校准的过程中，如果bottom - top的值差 不能小于原mapping的值差*MIN_VALUE_DIFF_RATIO

#define READ_BTNS_INTERVAL                  200            // 检查按钮状态间隔 us
#define ENABLED_DYNAMIC_CALIBRATION         1               //是否启用动态校准
#define DYNAMIC_CALIBRATION_INTERVAL        500000          // 动态校准间隔 500ms


#define NUM_GPIO_BUTTONS            4               //GPIO按钮数量
#define GPIO_BUTTONS_DEBOUNCE       1000             //去抖动延迟(us)  1ms

#define FN_BUTTON_VIRTUAL_PIN       (1U << (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS - 1))  // FN 键虚拟引脚 最后一个GPIO按钮

#define HAS_LED                                   1             //是否有LED
#define NUM_LED	                    (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS) //LED数量

#define NUM_LEDs_PER_ADC_BUTTON     1              //每个按钮多少个LED
#define LEDS_BRIGHTNESS_RATIO       0.8             //默认led 亮度系数 会以实际亮度乘以这个系数
#define LEDS_ANIMATION_CYCLE        6000            //LED 动画长度 ms
#define LEDS_ANIMATION_INTERVAL         2000          //LED 动画间隔，影响性能和效果 us

// #define LED_ENABLE_SWITCH_PIN        GPIO_PIN_12    // 灯效开关引脚
// #define LED_ENABLE_SWITCH_PORT       GPIOC           // 灯效开关端口

#define NUM_GAMEPAD_HOTKEYS                 (uint8_t)11   // 快捷键数量

// GPIO 按钮定义结构体
struct gpio_pin_def {
    GPIO_TypeDef* port;
    uint16_t pin;
    uint8_t virtualPin;
};

// Hall 按钮映射表 DMA to virtualPin，virtualPin 是按钮在所有按钮中的序号

/**ADC1 GPIO Configuration
PF11     ------> ADC1_INP2 -----> 2
PA6     ------> ADC1_INP3  -----> 7
PC4     ------> ADC1_INP4  -----> 4
PF12     ------> ADC1_INP6 -----> 0
PA7     ------> ADC1_INP7  -----> 5
PC5     ------> ADC1_INP8  -----> 6
*/
#define ADC1_BUTTONS_MAPPING_DMA_TO_VIRTUALPIN {2, 7, 4, 0, 5, 6}
/**ADC2 GPIO Configuration
PF13     ------> ADC2_INP2 -----> 1
PF14     ------> ADC2_INP6 -----> 3
PC1     ------> ADC2_INP11 -----> 13
PC2     ------> ADC2_INP12 -----> 11
PC3     ------> ADC2_INP13 -----> 8
PA2     ------> ADC2_INP14 -----> 9
*/
#define ADC2_BUTTONS_MAPPING_DMA_TO_VIRTUALPIN {1, 3, 13, 11, 8, 9}
/**ADC3 GPIO Configuration
PF5     ------> ADC3_INP4  -----> 15
PF3     ------> ADC3_INP5  -----> 16
PF4     ------> ADC3_INP9  -----> 14
PH2     ------> ADC3_INP13 -----> 12
PH3     ------> ADC3_INP14 -----> 10
*/
#define ADC3_BUTTONS_MAPPING_DMA_TO_VIRTUALPIN {15, 16, 14, 12, 10}


#ifdef __cplusplus
 }
#endif

#endif // __BOARD_H__


