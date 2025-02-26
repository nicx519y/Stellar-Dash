#ifndef _ENUMS_H_
#define _ENUMS_H_  

#include "stdint.h"

enum STORAGE_ERROR_NO {
    ACTION_SUCCESS      = 0,
    ACTION_FAILURE      = 1,
    PARAMETERS_ERROR    = 2,
};


enum BootMode
{
    BOOT_MODE_WEB_CONFIG                = 1,
    BOOT_MODE_ADC_BTNS_CALIBRATING      = 2,
    BOOT_MODE_INPUT                     = 3,
};

enum ConfigType
{
    CONFIG_TYPE_WEB = 0,
};

// 输入模式
enum InputMode
{
    INPUT_MODE_XINPUT = 0,
    INPUT_MODE_PS4,
    INPUT_MODE_SWITCH,
    INPUT_MODE_CONFIG = 255,
};

// 输入模式认证类型
enum InputModeAuthType
{
    INPUT_MODE_AUTH_TYPE_NONE = 0,
    INPUT_MODE_AUTH_TYPE_KEYS = 1,
    INPUT_MODE_AUTH_TYPE_USB = 2,
    INPUT_MODE_AUTH_TYPE_I2C = 3,
};


// enum DpadMode
// {
//     DPAD_MODE_DIGITAL,
//     DPAD_MODE_LEFT_ANALOG,
//     DPAD_MODE_RIGHT_ANALOG,
// };

enum SOCDMode
{
    SOCD_MODE_NEUTRAL,
    SOCD_MODE_UP_PRIORITY,
    SOCD_MODE_SECOND_INPUT_PRIORITY,
    SOCD_MODE_FIRST_INPUT_PRIORITY,
    SOCD_MODE_BYPASS,
};

enum LEDEffect
{
    STATIC              = 0,        //静态 恒亮
    BREATHING           = 1,        //呼吸
};

enum GamepadHotkey
{
    HOTKEY_NONE,                        // 无
    HOTKEY_LEDS_EFFECTSTYLE_NEXT,       // 切换LED效果
    HOTKEY_LEDS_EFFECTSTYLE_PREV,       // 切换LED效果
    HOTKEY_LEDS_BRIGHTNESS_UP,          // 增加LED亮度
    HOTKEY_LEDS_BRIGHTNESS_DOWN,        // 减少LED亮度
    HOTKEY_LEDS_ENABLE_SWITCH,          // 切换LED 开关
    HOTKEY_CALIBRATION_MODE,            // 切换到校准模式
    HOTKEY_INPUT_MODE_WEBCONFIG,        // 切换到web配置模式
    HOTKEY_INPUT_MODE_XINPUT,           // 切换到XInput模式
    HOTKEY_INPUT_MODE_PS4,              // 切换到PS4模式
    HOTKEY_INPUT_MODE_SWITCH,           // 切换到Switch模式
    HOTKEY_SYSTEM_REBOOT,               // 重启系统
};

enum GpioAction
{
    // the lowest value is the default, which should be NONE;
    // reserving some numbers in case we need more not-mapped type values
    NONE                         = -10,
    RESERVED                     = -5,
    ASSIGNED_TO_ADDON            = 0,
    BUTTON_PRESS_UP              = 1,
    BUTTON_PRESS_DOWN            = 2,
    BUTTON_PRESS_LEFT            = 3,
    BUTTON_PRESS_RIGHT           = 4,
    BUTTON_PRESS_B1              = 5,
    BUTTON_PRESS_B2              = 6,
    BUTTON_PRESS_B3              = 7,
    BUTTON_PRESS_B4              = 8,
    BUTTON_PRESS_L1              = 9,
    BUTTON_PRESS_R1              = 10,
    BUTTON_PRESS_L2              = 11,
    BUTTON_PRESS_R2              = 12,
    BUTTON_PRESS_S1              = 13,
    BUTTON_PRESS_S2              = 14,
    BUTTON_PRESS_A1              = 15,
    BUTTON_PRESS_A2              = 16,
    BUTTON_PRESS_L3              = 17,
    BUTTON_PRESS_R3              = 18,
    BUTTON_PRESS_FN              = 19,
    BUTTON_PRESS_DDI_UP          = 20,
    BUTTON_PRESS_DDI_DOWN        = 21,
    BUTTON_PRESS_DDI_LEFT        = 22,
    BUTTON_PRESS_DDI_RIGHT       = 23,
    SUSTAIN_DP_MODE_DP           = 24,
    SUSTAIN_DP_MODE_LS           = 25,
    SUSTAIN_DP_MODE_RS           = 26,
    SUSTAIN_SOCD_MODE_UP_PRIO    = 27,
    SUSTAIN_SOCD_MODE_NEUTRAL    = 28,
    SUSTAIN_SOCD_MODE_SECOND_WIN = 29,
    SUSTAIN_SOCD_MODE_FIRST_WIN  = 30,
    SUSTAIN_SOCD_MODE_BYPASS     = 31,
    BUTTON_PRESS_TURBO           = 32,
    BUTTON_PRESS_MACRO           = 33,
    BUTTON_PRESS_MACRO_1         = 34,
    BUTTON_PRESS_MACRO_2         = 35,
    BUTTON_PRESS_MACRO_3         = 36,
    BUTTON_PRESS_MACRO_4         = 37,
    BUTTON_PRESS_MACRO_5         = 38,
    BUTTON_PRESS_MACRO_6         = 39,
    CUSTOM_BUTTON_COMBO          = 40,
    BUTTON_PRESS_A3              = 41,
    BUTTON_PRESS_A4              = 42,
    BUTTON_PRESS_E1              = 43,
    BUTTON_PRESS_E2              = 44,
    BUTTON_PRESS_E3              = 45,
    BUTTON_PRESS_E4              = 46,
    BUTTON_PRESS_E5              = 47,
    BUTTON_PRESS_E6              = 48,
    BUTTON_PRESS_E7              = 49,
    BUTTON_PRESS_E8              = 50,
    BUTTON_PRESS_E9              = 51,
    BUTTON_PRESS_E10             = 52,
    BUTTON_PRESS_E11             = 53,
    BUTTON_PRESS_E12             = 54
};

enum ADCButtonManagerState
{
    WORKING     = 0,
    CALIBRATING = 1,
};

enum ADCButtonState
{
    NOT_READY           = 0,
    CALIBRATING_TOP     = 1,
    CALIBRATING_BOTTOM  = 2,
    READY               = 3,
};

#endif /* _NET_DRIVER_H_  */