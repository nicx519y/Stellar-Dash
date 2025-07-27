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

// STM32H7系列的唯一ID存储地址（96位，12字节）
#define STM32_UNIQUE_ID_BASE_ADDR   0x1FF1E800

#ifndef FPU_FPDSCR_RMode_Msk
    #define FPU_FPDSCR_RMode_Msk   (0x3 << 22) // 清除舍入模式位 [23:22]
#endif

#ifndef FPU_FPDSCR_RMode_RN
    #define FPU_FPDSCR_RMode_RN    (0x0 << 22) // 舍入模式为 Round to Nearest (RN)  
#endif

/* Debug print configuration */
#define APPLICATION_DEBUG_PRINT  0   // 设置为 0 可以关闭所有调试打印

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

#define USB_DEBUG_PRINT 0

#if USB_DEBUG_PRINT
    #define USB_DBG(fmt, ...) printf("[USB] " fmt "\r\n", ##__VA_ARGS__)
    #define USB_ERR(fmt, ...) printf("[USB][ERROR] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define USB_DBG(fmt, ...) ((void)0)
    #define USB_ERR(fmt, ...) ((void)0)
#endif



#define CONFIG_VERSION                      (uint32_t)0x00000d  //配置版本 三位版本号 0x aa bb cc
#define ADC_MAPPING_VERSION                 (uint32_t)0x000001  //ADC值映射表版本

// 双槽地址偏移定义（相对于槽基地址的偏移）
#define WEB_RESOURCES_OFFSET                0x00100000      // WebResources偏移 +1MB
#define ADC_VALUES_MAPPING_OFFSET           0x00280000      // ADC值映射表偏移 +2.5MB  

// 用户配置区固定地址（独立于槽，两个槽共享）
#define CONFIG_ADDR                         0x90700000      // 用户配置区固定地址

// 动态地址获取函数声明（需要在相应的.c文件中实现）
uint32_t get_current_slot_base_address(void);

// 动态地址宏定义（基于当前槽基地址）
#define WEB_RESOURCES_ADDR                  (get_current_slot_base_address() + WEB_RESOURCES_OFFSET)
#define ADC_VALUES_MAPPING_ADDR             (get_current_slot_base_address() + ADC_VALUES_MAPPING_OFFSET)

// 兼容性：如果需要编译时确定的地址，可以使用默认槽A地址
#define WEB_RESOURCES_ADDR_STATIC           (0x90000000 + WEB_RESOURCES_OFFSET)       // 0x90100000
#define ADC_VALUES_MAPPING_ADDR_STATIC      (0x90000000 + ADC_VALUES_MAPPING_OFFSET)  // 0x90280000

#define NUM_ADC_VALUES_MAPPING              8               // 最大8个映射 ADC按钮映射表用于值查找
#define MAX_ADC_VALUES_LENGTH               40              // 每个映射最大40个值 ADC按钮映射表用于值查找
#define MAX_NUM_MARKING_VALUE               100             // 每个step最大采集值个数
#define TIME_ADC_INIT                       1000            // ADC初始化时间，时间越长初始化越准确
#define NUM_WINDOW_SIZE                     8               // 校准滑动窗口大小

#define ULTRAFast_THRESHOLD_NONE                 0               // 超快版本阈值 0 表示不使用防抖
#define ULTRAFast_THRESHOLD_MAX             30             // 超快版本阈值最大值
#define ULTRAFast_THRESHOLD_NORMAL          15             // 超快版本阈值一般值


#define NUM_PROFILES                        16
#define NUM_ADC                             3               // 3个ADC
#define NUM_ADC1_BUTTONS                    6
#define NUM_ADC2_BUTTONS                    6
#define NUM_ADC3_BUTTONS                    6
#define NUM_ADC_BUTTONS                     (NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS + NUM_ADC3_BUTTONS)
#define MIN_ADC_TOP_DEADZONE                0.1             // 默认ADC顶部死区最小值
#define MIN_ADC_BOTTOM_DEADZONE             0.1             // 默认ADC底部死区最小值
#define MIN_ADC_RELEASE_ACCURACY            0.1f            // 默认ADC释放精度
#define MIN_VALUE_DIFF_RATIO                0.8             // 最小值差值比例 按键动态校准的过程中，如果bottom - top的值差 不能小于原mapping的值差*MIN_VALUE_DIFF_RATIO

#define READ_BTNS_INTERVAL                  50            // 检查按钮状态间隔 us
#define DYNAMIC_CALIBRATION_INTERVAL        500000          // 动态校准间隔 500ms

// ========== WebConfig模式ADC按键专用配置宏定义 ==========
#define WEBCONFIG_ADC_DEFAULT_PRESS_ACCURACY     1.0f      // WebConfig模式下默认按下精度（mm 设置为1 防止误触
#define WEBCONFIG_ADC_DEFAULT_RELEASE_ACCURACY   0.2f      // WebConfig模式下默认释放精度（mm）
#define WEBCONFIG_ADC_DEFAULT_TOP_DEADZONE       0.2f       // WebConfig模式下默认顶部死区（mm） - 更小
#define WEBCONFIG_ADC_DEFAULT_BOTTOM_DEADZONE    0.2f       // WebConfig模式下默认底部死区（mm）
#define WEBCONFIG_ADC_DEFAULT_HIGH_SENSITIVITY   false       // WebConfig模式下默认启用高敏感度

#define NUM_GPIO_BUTTONS            4               //GPIO按钮数量
#define GPIO_BUTTONS_DEBOUNCE       1000             //去抖动延迟(us)  1ms

#define FN_BUTTON_VIRTUAL_PIN       (1U << (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS - 1))  // FN 键虚拟引脚 最后一个GPIO按钮

#define HAS_LED                                   1             //是否有LED
#define HAS_LED_AROUND                            1          //是否有底部环绕led
#define NUM_LED_AROUND                            37          //底部环绕led数量
#define NUM_LED	                    (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS + NUM_LED_AROUND) //LED数量

#define NUM_LEDs_PER_ADC_BUTTON     1              //每个按钮多少个LED
#define LEDS_BRIGHTNESS_RATIO       0.8             //默认led 亮度系数 会以实际亮度乘以这个系数
#define LEDS_ANIMATION_CYCLE        10000            //LED 动画长度 ms
#define LEDS_ANIMATION_INTERVAL         16          //LED 动画间隔，影响性能和效果 ms

// #define LED_ENABLE_SWITCH_PIN        GPIO_PIN_12    // 灯效开关引脚
// #define LED_ENABLE_SWITCH_PORT       GPIOC           // 灯效开关端口

#define NUM_GAMEPAD_HOTKEYS                 (uint8_t)11   // 快捷键数量
#define HOLD_THRESHOLD_MS                   1000             // 长按阈值 1000ms

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
PC1     ------> ADC2_INP11 -----> 14
PC2     ------> ADC2_INP12 -----> 12
PC3     ------> ADC2_INP13 -----> 8
PA2     ------> ADC2_INP14 -----> 9
*/
#define ADC2_BUTTONS_MAPPING_DMA_TO_VIRTUALPIN {1, 3, 14, 12, 8, 9}
/**ADC3 GPIO Configuration
PF5     ------> ADC3_INP4  -----> 16
PF3     ------> ADC3_INP5  -----> 17
PF4     ------> ADC3_INP9  -----> 15
PH2     ------> ADC3_INP13 -----> 13
PH3     ------> ADC3_INP14 -----> 10
PH4     ------> ADC3_INP15 -----> 11
*/
#define ADC3_BUTTONS_MAPPING_DMA_TO_VIRTUALPIN {16, 17, 15, 13, 10, 11}

#define ADC_CALIBRATION_MANAGER_REQUIRED_SAMPLES 100 // 校准管理器需要的采样数量
#define ADC_CALIBRATION_MANAGER_SAMPLE_INTERVAL_MS 1 // 校准管理器采样间隔（毫秒）
#define ADC_CALIBRATION_MANAGER_TOLERANCE_RANGE 5000 // 校准管理器容忍范围
#define ADC_CALIBRATION_MANAGER_STABILITY_THRESHOLD 200 // 校准管理器稳定性阈值

#ifdef __cplusplus
 }
#endif

#endif // __BOARD_H__


