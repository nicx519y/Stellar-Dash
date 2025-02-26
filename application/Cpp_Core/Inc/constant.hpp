#ifndef _CONSTANT_H_
#define _CONSTANT_H_ 

#include <stdint.h>  // 为 uint8_t, uint32_t 等类型
#include "stm32h7xx.h"  // 为 GPIO_TypeDef 和 GPIO_PIN_x 定义



// C 代码部分
#ifdef __cplusplus
extern "C" {
#endif

#define FIRMWARE_VERSION                    (uint32_t)0x010000  //固件版本
#define CONFIG_VERSION                      (uint32_t)0x000100  //配置版本 三位版本号 0x aa bb cc
#define ADC_MAPPING_VERSION                 (uint32_t)0x000001  //ADC值映射表版本

#define WEB_RESOURCES_ADDR                  0x90100000       // 网页资源地址 memory map 地址 qspi flash 0x90100000
#define ADC_VALUES_MAPPING_ADDR             0x00200000       // ADC值映射表地址  qspi flash 0x00200000
#define CONFIG_ADDR                         0x00300000       // 配置数据地址  qspi flash 0x00300000

#define NUM_ADC_VALUES_MAPPING              8             // 最大8个映射 ADC按钮映射表用于值查找
#define MAX_ADC_VALUES_LENGTH               40             // 每个映射最大40个值 ADC按钮映射表用于值查找
#define MAX_NUM_MARKING_VALUE               100            // 每个step最大采集值个数
#define TIME_ADC_INIT                       1000           // ADC初始化时间，时间越长初始化越准确
#define NUM_WINDOW_SIZE                     8             // 校准滑动窗口大小

#define NUM_PROFILES                        16
#define NUM_ADC                             3             // 3个ADC
#define NUM_ADC1_BUTTONS                    6
#define NUM_ADC2_BUTTONS                    6
#define NUM_ADC3_BUTTONS                    5
#define NUM_ADC_BUTTONS                     (NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS + NUM_ADC3_BUTTONS)

#define READ_BTNS_INTERVAL                  50            // 检查按钮状态间隔 us
#define ENABLED_DYNAMIC_CALIBRATION         1               //是否启用动态校准
#define DYNAMIC_CALIBRATION_INTERVAL        (5 * 1000000)            // 动态校准间隔 5s

// Hall 按钮映射表 DMA to virtualPin，virtualPin 是按钮在所有按钮中的序号
static __attribute__((section(".rodata"))) const uint8_t ADC1_BUTTONS_MAPPING[NUM_ADC1_BUTTONS] = {1, 8, 9, 6, 0, 5};
static __attribute__((section(".rodata"))) const uint8_t ADC2_BUTTONS_MAPPING[NUM_ADC2_BUTTONS] = {2, 3, 7, 4, 14, 11};
static __attribute__((section(".rodata"))) const uint8_t ADC3_BUTTONS_MAPPING[NUM_ADC3_BUTTONS] = {13, 15, 16, 10, 12};

#define NUM_GPIO_BUTTONS            4               //GPIO按钮数量
#define GPIO_BUTTONS_DEBOUNCE       50             //去抖动延迟(us) 

// GPIO 按钮定义结构体
struct gpio_pin_def {
    GPIO_TypeDef* port;
    uint16_t pin;
    uint8_t virtualPin;
};

// GPIO 按钮映射表 GPIO to virtualPin
static __attribute__((section(".rodata"))) const struct gpio_pin_def GPIO_BUTTONS_MAPPING[NUM_GPIO_BUTTONS] = {
    { GPIOC, GPIO_PIN_6, 17 },
    { GPIOC, GPIO_PIN_7, 18 },
    { GPIOC, GPIO_PIN_8, 19 },
    { GPIOC, GPIO_PIN_9, 20 }
};

#define HAS_LED                                   1             //是否有LED
#define NUM_LED	                    (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS) //LED数量

#define NUM_LEDs_PER_ADC_BUTTON     1              //每个按钮多少个LED
#define LEDS_BRIGHTNESS_RADIO       0.3             //默认led 亮度系数 会以实际亮度乘以这个系数
#define LEDS_ANIMATION_CYCLE        6000            //LED 动画长度
#define LEDS_ANIMATION_INTERVAL         80          //LED 动画间隔，影响性能和效果 ms

#define LED_ENABLE_SWITCH_PIN        GPIO_PIN_12    // 灯效开关引脚
#define LED_ENABLE_SWITCH_PORT       GPIOC           // 灯效开关端口

#define NUM_GAMEPAD_HOTKEYS                 (uint8_t)11   // 快捷键数量

// 调试开关
#define DEBUG_ADC 1

#ifdef DEBUG_ADC
    #define ADC_DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
    #define ADC_DEBUG_PRINT(...)
#endif

#ifdef __cplusplus
}
#endif

#endif // _CONSTANT_H_