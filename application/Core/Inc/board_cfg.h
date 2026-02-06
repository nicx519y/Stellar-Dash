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
#include "stm32h7xx_hal.h"
#include <stdbool.h>

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

#define WEBCONFIG_IP_FIRST                  192
#define WEBCONFIG_IP_SECOND                 168
#define WEBCONFIG_IP_THIRD                  7
#define WEBCONFIG_IP_FOURTH                 1
#define WEBCONFIG_DOMAIN_NAME               "st-dash.usb"

#define CONFIG_VERSION                      (uint32_t)0x00000f  //配置版本 三位版本号 0x aa bb cc
#define ADC_MAPPING_VERSION                 (uint32_t)0x000001  //ADC值映射表版本
#define ADC_COMMON_VERSION                  (uint32_t)0x000001

// 双槽地址偏移定义（相对于槽基地址的偏移）
#define WEB_RESOURCES_OFFSET                0x00100000      // WebResources偏移 +1MB
#define ADC_VALUES_MAPPING_OFFSET           0x00280000      // ADC值映射表偏移 +2.5MB  

// 用户配置区固定地址（独立于槽，两个槽共享）
#define CONFIG_ADDR                         0x90700000      // 用户配置区固定地址



// ADC 引脚配置结构体
typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
    uint32_t channel;
    uint32_t rank;
    uint8_t virtualPin;
} ADC_PinConfig;

static const ADC_PinConfig ADC1_PIN_MAP[] = {
    { GPIOF, GPIO_PIN_11, ADC_CHANNEL_2,  ADC_REGULAR_RANK_1, 2 },
    { GPIOA, GPIO_PIN_6,  ADC_CHANNEL_3,  ADC_REGULAR_RANK_2, 7 },
    { GPIOC, GPIO_PIN_4,  ADC_CHANNEL_4,  ADC_REGULAR_RANK_3, 4 },
    { GPIOF, GPIO_PIN_12, ADC_CHANNEL_6,  ADC_REGULAR_RANK_4, 0 },
    { GPIOA, GPIO_PIN_7,  ADC_CHANNEL_7,  ADC_REGULAR_RANK_5, 5 },
    { GPIOC, GPIO_PIN_5,  ADC_CHANNEL_8,  ADC_REGULAR_RANK_6, 6 },
};

static const ADC_PinConfig ADC2_PIN_MAP[] = {
    { GPIOF, GPIO_PIN_13, ADC_CHANNEL_2,  ADC_REGULAR_RANK_1, 1 },
    { GPIOF, GPIO_PIN_14, ADC_CHANNEL_6,  ADC_REGULAR_RANK_2, 3 },
    { GPIOC, GPIO_PIN_1,  ADC_CHANNEL_11, ADC_REGULAR_RANK_3, 14 },
    { GPIOC, GPIO_PIN_2,  ADC_CHANNEL_12, ADC_REGULAR_RANK_4, 12 },
    { GPIOC, GPIO_PIN_3,  ADC_CHANNEL_13, ADC_REGULAR_RANK_5, 8 },
    { GPIOA, GPIO_PIN_2,  ADC_CHANNEL_14, ADC_REGULAR_RANK_6, 9 },
};

static const ADC_PinConfig ADC3_PIN_MAP[] = {
    { GPIOF, GPIO_PIN_5,  ADC_CHANNEL_4,  ADC_REGULAR_RANK_1, 16 },
    { GPIOF, GPIO_PIN_4,  ADC_CHANNEL_9,  ADC_REGULAR_RANK_2, 15 },
    { GPIOH, GPIO_PIN_2,  ADC_CHANNEL_13, ADC_REGULAR_RANK_3, 13 },
    { GPIOH, GPIO_PIN_3,  ADC_CHANNEL_14, ADC_REGULAR_RANK_4, 10 },
    { GPIOH, GPIO_PIN_4,  ADC_CHANNEL_15, ADC_REGULAR_RANK_5, 11 },
};

#define ADC1_PIN_MAP_SIZE (sizeof(ADC1_PIN_MAP)/sizeof(ADC_PinConfig))
#define ADC2_PIN_MAP_SIZE (sizeof(ADC2_PIN_MAP)/sizeof(ADC_PinConfig))
#define ADC3_PIN_MAP_SIZE (sizeof(ADC3_PIN_MAP)/sizeof(ADC_PinConfig))

#define ADC_CALIBRATION_MANAGER_REQUIRED_SAMPLES 100 // 校准管理器需要的采样数量
#define ADC_CALIBRATION_MANAGER_SAMPLE_INTERVAL_MS 1 // 校准管理器采样间隔（毫秒）
#define ADC_CALIBRATION_MANAGER_TOLERANCE_RANGE 8000 // 校准管理器容忍范围
#define ADC_CALIBRATION_MANAGER_STABILITY_THRESHOLD 400 // 校准管理器稳定性阈值

// GPIO 按钮定义结构体
struct gpio_pin_def {
    GPIO_TypeDef* port;
    uint16_t pin;
    uint8_t virtualPin;
};

// GPIO按钮配置宏定义
#define GPIO_BTN1_PORT              GPIOC
#define GPIO_BTN1_PIN               GPIO_PIN_6
#define GPIO_BTN1_VIRTUAL_PIN       17

#define GPIO_BTN2_PORT              GPIOC
#define GPIO_BTN2_PIN               GPIO_PIN_7
#define GPIO_BTN2_VIRTUAL_PIN       18

#define GPIO_BTN3_PORT              GPIOC
#define GPIO_BTN3_PIN               GPIO_PIN_8
#define GPIO_BTN3_VIRTUAL_PIN       19

#define GPIO_BTN4_PORT              GPIOC
#define GPIO_BTN4_PIN               GPIO_PIN_9
#define GPIO_BTN4_VIRTUAL_PIN       20


// 动态地址获取函数声明（需要在相应的.c文件中实现）
uint32_t get_current_slot_base_address(void);

// 动态地址宏定义（基于当前槽基地址）
#define WEB_RESOURCES_ADDR                  (get_current_slot_base_address() + WEB_RESOURCES_OFFSET)
#define ADC_VALUES_MAPPING_ADDR             (get_current_slot_base_address() + ADC_VALUES_MAPPING_OFFSET)

// 兼容性：如果需要编译时确定的地址，可以使用默认槽A地址
#define WEB_RESOURCES_ADDR_STATIC           (0x90000000 + WEB_RESOURCES_OFFSET)       // 0x90100000
#define ADC_VALUES_MAPPING_ADDR_STATIC      (0x90000000 + ADC_VALUES_MAPPING_OFFSET)  // 0x90280000

#define ADC_COMMON_CONFIG_ADDR              0x90710000

#define NUM_ADC_VALUES_MAPPING              8               // 最大8个映射 ADC按钮映射表用于值查找
#define MAX_ADC_VALUES_LENGTH               40              // 每个映射最大40个值 ADC按钮映射表用于值查找
#define MAX_NUM_MARKING_VALUE               100             // 每个step最大采集值个数
#define TIME_ADC_INIT                       1000            // ADC初始化时间，时间越长初始化越准确
#define NUM_WINDOW_SIZE                     8               // 校准滑动窗口大小

#define NUM_PROFILES                        16
#define NUM_ADC                             3               // 3个ADC
#define NUM_ADC1_BUTTONS                    ADC1_PIN_MAP_SIZE
#define NUM_ADC2_BUTTONS                    ADC2_PIN_MAP_SIZE
#define NUM_ADC3_BUTTONS                    ADC3_PIN_MAP_SIZE
#define NUM_ADC_BUTTONS                     (NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS + NUM_ADC3_BUTTONS)
#define MIN_ADC_TOP_DEADZONE                0.1             // 默认ADC顶部死区最小值
#define MIN_ADC_BOTTOM_DEADZONE             0.1             // 默认ADC底部死区最小值
#define MIN_ADC_RELEASE_ACCURACY            0.1f            // 默认ADC释放精度
#define MIN_VALUE_DIFF_RATIO                0.8             // 最小值差值比例 按键动态校准的过程中，如果bottom - top的值差 不能小于原mapping的值差*MIN_VALUE_DIFF_RATIO

#define MAX_KEY_COMBINATION                 10              // 最大自定义按键组合键数量
#define MAX_KEY_COMBINATION_WEBCONFIG       5               // 最大自定义按键组合键数量 webconfig模式下，实际使用数量 必须小于等于 MAX_KEY_COMBINATION

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

#define NUM_LEDs_PER_ADC_BUTTON     1              //每个按钮多少个LED
#define LEDS_BRIGHTNESS_RATIO       0.8             //默认led 亮度系数 会以实际亮度乘以这个系数
#define LEDS_ANIMATION_CYCLE        10000            //LED 动画长度 ms
#define LEDS_ANIMATION_INTERVAL         16          //LED 动画间隔，影响性能和效果 ms

#define WEBCONFIG_BUTTON_PERFORMANCE_MONITORING_INTERVAL_MS 100 // 按键性能监控间隔 ms

// #define LED_ENABLE_SWITCH_PIN        GPIO_PIN_12    // 灯效开关引脚
// #define LED_ENABLE_SWITCH_PORT       GPIOC           // 灯效开关端口

#define NUM_GAMEPAD_HOTKEYS                 (uint8_t)11   // 快捷键数量
#define HOLD_THRESHOLD_MS                   1000             // 长按阈值 1000ms

#define HAS_LED                                   1             //是否有LED
// #define HAS_LED_AROUND                            1          //是否有底部环绕led
extern bool g_has_led_around;                               // 运行时检测是否有氛围灯
#define NUM_LED_AROUND                            49          //底部环绕led数量
#define NUM_LED	                    (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS + NUM_LED_AROUND) //LED数量

// ================= LED 位置定义 =================
// 为在 C/C++ 两端使用，将按钮位置结构体和坐标数组放到此处
typedef struct Position {
    float x;
    float y;
    float r;
} Position;

#define HITBOX_ADC_BUTTON_POS_DATA \
    { 125.10f,  103.10f,  26.00f },      /* 0 */ \
    { 147.34f,  120.10f,  34.00f },      /* 1 */ \
    { 175.10f,  119.10f,  26.00f },      /* 2 */ \
    { 192.80f,  101.44f,  26.00f },      /* 3 */ \
    { 73.49f,   63.76f,   26.00f },        /* 4 */ \
    { 99.05f,   59.67f,   26.00f },        /* 5 */ \
    { 122.19f,  63.76f,   26.00f },       /* 6 */ \
    { 141.44f,  77.23f,   26.00f },       /* 7 */ \
    { 131.19f,  42.04f,   26.00f },       /* 8 */ \
    { 165.45f,  87.10f,   26.00f },       /* 9 */ \
    { 163.37f,  62.80f,   26.00f },       /* 10 */ \
    { 185.51f,  73.05f,   26.00f },       /* 11 */ \
    { 183.43f,  48.75f,   26.00f },       /* 12 */ \
    { 209.01f,  66.10f,   26.00f },       /* 13 */ \
    { 206.93f,  41.80f,   26.00f },       /* 14 */ \
    { 233.44f,  67.98f,   26.00f },       /* 15 */ \
    { 231.36f,  43.69f,   26.00f }       /* 16 */ 
    

#define HITBOX_GPIO_BUTTON_POS_DATA \
    { 84.49f,   15.49f,   11.50f },        /* 17 */ \
    { 62.49f,   15.49f,   11.50f },        /* 18 */ \
    { 40.49f,   15.49f,   11.50f },        /* 19 */ \
    { 18.48f,   15.49f,   11.50f }        /* 20 */

#define HITBOX_AMBIENT_POS_DATA \
    { 35.10f, 35.10f, 5.40f },         /* 22 */ \
    { 35.10f, 45.10f, 5.40f },         /* 23 */ \
    { 35.10f, 55.10f, 5.40f },          /* 24 */ \
    { 35.10f, 65.10f, 5.40f },         /* 25 */ \
    { 35.10f, 75.10f, 5.40f },         /* 26 */ \
    { 35.10f, 85.10f, 5.40f },         /* 27 */ \
    { 35.10f, 95.10f, 5.40f },         /* 28 */ \
    { 35.10f, 105.10f, 5.40f },        /* 29 */ \
    { 35.10f, 115.10f, 5.40f },        /* 30 */ \
    { 35.10f, 125.10f, 5.40f },        /* 31 */ \
    { 35.10f, 135.10f, 5.40f },        /* 32 */ \
    { 35.10f, 145.10f, 5.40f },        /* 33 */ \
    { 35.10f, 155.10f, 5.40f },        /* 34 */ \
    \
    { 45.10f, 155.10f, 5.40f },        /* 35 */ \
    { 55.10f, 155.10f, 5.40f },        /* 36 */ \
    { 65.10f, 155.10f, 5.40f },       /* 37 */ \
    { 75.10f, 155.10f, 5.40f },       /* 38 */ \
    { 85.10f, 155.10f, 5.40f },       /* 39 */ \
    { 95.10f, 155.10f, 5.40f },       /* 40 */ \
    { 105.10f, 155.10f, 5.40f },       /* 41 */ \
    { 115.10f, 155.10f, 5.40f },       /* 42 */ \
    { 125.10f, 155.10f, 5.40f },       /* 43 */ \
    { 135.10f, 155.10f, 5.40f },       /* 44 */ \
    { 145.10f, 155.10f, 5.40f },       /* 45 */ \
    { 155.10f, 155.10f, 5.40f },       /* 40 */ \
    { 165.10f, 155.10f, 5.40f },       /* 41 */ \
    { 175.10f, 155.10f, 5.40f },       /* 42 */ \
    { 185.10f, 155.10f, 5.40f },       /* 43 */ \
    { 195.10f, 155.10f, 5.40f },       /* 44 */ \
    { 205.10f, 155.10f, 5.40f },       /* 45 */ \
    { 215.10f, 155.10f, 5.40f },       /* 46 */ \
    { 225.10f, 155.10f, 5.40f },       /* 47 */ \
    { 235.10f, 155.10f, 5.40f },       /* 48 */ \
    { 245.10f, 155.10f, 5.40f },       /* 49 */ \
    { 255.10f, 155.10f, 5.40f },       /* 50 */ \
    { 265.10f, 155.10f, 5.40f },       /* 51 */ \
    \
    { 275.10f, 155.10f, 5.40f },        /* 52 */ \
    { 275.10f, 145.10f, 5.40f },        /* 53 */ \
    { 275.10f, 135.10f, 5.40f },        /* 54 */ \
    { 275.10f, 125.10f, 5.40f },        /* 55 */ \
    { 275.10f, 115.10f, 5.40f },        /* 56 */ \
    { 275.10f, 105.10f, 5.40f },        /* 57 */ \
    { 275.10f, 95.10f, 5.40f },         /* 58 */ \
    { 275.10f, 85.10f, 5.40f },         /* 59 */ \
    { 275.10f, 75.10f, 5.40f },         /* 60 */ \
    { 275.10f, 65.10f, 5.40f },         /* 61 */ \
    { 275.10f, 55.10f, 5.40f },         /* 62 */ \
    { 275.10f, 45.10f, 5.40f },         /* 63 */ \
    { 275.10f, 35.10f, 5.40f }          /* 64 */

static const Position HITBOX_BUTTON_POS_LIST[NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS] = {
    HITBOX_ADC_BUTTON_POS_DATA,
    HITBOX_GPIO_BUTTON_POS_DATA
};

static const Position HITBOX_AMBIENT_POS_LIST[NUM_LED_AROUND] = {
    HITBOX_AMBIENT_POS_DATA
};

static const Position HITBOX_LED_POS_LIST[NUM_LED] = {
    HITBOX_ADC_BUTTON_POS_DATA,
    HITBOX_GPIO_BUTTON_POS_DATA

    , HITBOX_AMBIENT_POS_DATA
};


#ifdef __cplusplus
#include "enums.hpp"

// Default hotkey configuration
// The default hotkey configuration is used when the user has not configured any hotkey.
// The default hotkey configuration is also used when the user has configured a hotkey, but the hotkey is not available on the current gamepad.

typedef struct {
    bool isLocked;
    GamepadHotkey action;
    bool isHold;
    int32_t virtualPin;
} DefaultHotkeyConfig;

static const DefaultHotkeyConfig DEFAULT_HOTKEY_LIST[NUM_GAMEPAD_HOTKEYS] = {
    { true,  GamepadHotkey::HOTKEY_INPUT_MODE_WEBCONFIG,           true,  19 }, // 0
    { true,  GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION,         true,  18 }, // 1
    { false, GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_NEXT,          false, 15 }, // 2
    { false, GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_PREV,          false, 16 }, // 3
    { false, GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_UP,             false, 14 }, // 4
    { false, GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_DOWN,           false, 13 }, // 5
    { false, GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_NEXT, false, 11 }, // 6
    { false, GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_PREV, false, 12 }, // 7
    { false, GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_UP,    false, 10 }, // 8
    { false, GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_DOWN,  false, 9  }, // 9
    { false, GamepadHotkey::HOTKEY_LEDS_ENABLE_SWITCH,             true,  2  }, // 10
};
#endif


#ifdef __cplusplus
 }
#endif

#endif // __BOARD_H__


