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
    BOOT_MODE_INPUT                     = 2,
    BOOT_MODE_CALIBRATION               = 3,
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
    INPUT_MODE_PS5,
    INPUT_MODE_SWITCH,
    INPUT_MODE_XBONE,
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
    NUM_SOCD_MODES,
};

enum LEDEffect
{
    STATIC              = 0,        //静态 恒亮
    BREATHING           = 1,        //呼吸
    STAR                = 2,        //星光闪烁
    FLOWING             = 3,        //流光
    RIPPLE              = 4,        //涟漪
    TRANSFORM           = 5,        //变换
    NUM_EFFECTS         = 6,        //效果总数
};

enum AroundLEDEffect
{
    AROUND_STATIC     = 0,
    AROUND_BREATHING  = 1,
    AROUND_QUAKE      = 2,
    AROUND_METEOR     = 3,
    NUM_AROUND_LED_EFFECTS,
};

enum GamepadHotkey
{
    HOTKEY_NONE,                        // 无
    HOTKEY_LEDS_ENABLE_SWITCH,          // 切换LED 开关
    HOTKEY_LEDS_EFFECTSTYLE_NEXT,       // 切换LED效果
    HOTKEY_LEDS_EFFECTSTYLE_PREV,       // 切换LED效果
    HOTKEY_LEDS_BRIGHTNESS_UP,          // 增加LED亮度
    HOTKEY_LEDS_BRIGHTNESS_DOWN,        // 减少LED亮度
    HOTKEY_AMBIENT_LIGHT_ENABLE_SWITCH, // 切换氛围灯 开关
    HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_NEXT, // 切换氛围灯效果
    HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_PREV, // 切换氛围灯效果
    HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_UP,   // 增加氛围灯亮度
    HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_DOWN, // 减少氛围灯亮度
    HOTKEY_CALIBRATION_MODE,            // 切换到校准模式
    HOTKEY_INPUT_MODE_WEBCONFIG,        // 切换到web配置模式
    HOTKEY_INPUT_MODE_CALIBRATION,      // 切换到校准模式
    HOTKEY_INPUT_MODE_XINPUT,           // 切换到XInput模式
    HOTKEY_INPUT_MODE_PS4,              // 切换到PS4模式
    HOTKEY_INPUT_MODE_PS5,              // 切换到PS5模式
    HOTKEY_INPUT_MODE_XBONE,            // 切换到XBONE模式
    HOTKEY_INPUT_MODE_SWITCH,           // 切换到Switch模式
    HOTKEY_SYSTEM_REBOOT,               // 重启系统
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

enum PS4ControllerType
{
    PS4_CONTROLLER = 0,
    PS4_ARCADESTICK = 7,
};

enum ADCButtonDebounceAlgorithm
{
    NONE = 0,
    NORMAL = 1,
    MAX = 2,
    NUM_ADC_BUTTON_DEBOUNCE_ALGORITHMS,
};

enum GameControllerButton
{
    GAME_CONTROLLER_NONE = 0,
    GAME_CONTROLLER_DPAD_UP = 1,
    GAME_CONTROLLER_DPAD_DOWN = 2,
    GAME_CONTROLLER_DPAD_LEFT = 3, 
    GAME_CONTROLLER_DPAD_RIGHT = 4,
    GAME_CONTROLLER_BUTTON_B1 = 5,
    GAME_CONTROLLER_BUTTON_B2 = 6,
    GAME_CONTROLLER_BUTTON_B3 = 7,
    GAME_CONTROLLER_BUTTON_B4 = 8,
    GAME_CONTROLLER_BUTTON_L1 = 9,
    GAME_CONTROLLER_BUTTON_R1 = 10,
    GAME_CONTROLLER_BUTTON_L2 = 11,
    GAME_CONTROLLER_BUTTON_R2 = 12,
    GAME_CONTROLLER_BUTTON_S1 = 13,
    GAME_CONTROLLER_BUTTON_S2 = 14,
    GAME_CONTROLLER_BUTTON_L3 = 15,
    GAME_CONTROLLER_BUTTON_R3 = 16,
    GAME_CONTROLLER_BUTTON_A1 = 17,
    GAME_CONTROLLER_BUTTON_A2 = 18,
    GAME_CONTROLLER_BUTTON_FN = 19,
    NUM_GAME_CONTROLLER_BUTTONS,
};

#endif /* _NET_DRIVER_H_  */