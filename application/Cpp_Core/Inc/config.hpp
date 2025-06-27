#ifndef CONFIG_H
#define CONFIG_H

#include <string.h>
#include "enums.hpp"
#include "stm32h750xx.h"
#include "stm32h7xx_hal.h"
#include "board_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    SOCDMode socdMode;
    bool fourWayMode;
    bool invertXAxis;
    bool invertYAxis;
    uint32_t keyDpadUp;             // example: 0001 0000 0010 0000 0000 0000 0000 0000  说明 keyDpadUp 这个功能按键和物理按键virtualPin的关系
    uint32_t keyDpadDown;
    uint32_t keyDpadLeft;
    uint32_t keyDpadRight;
    uint32_t keyButtonB1;
    uint32_t keyButtonB2;
    uint32_t keyButtonB3;
    uint32_t keyButtonB4;
    uint32_t keyButtonL1;
    uint32_t keyButtonR1;
    uint32_t keyButtonL2;
    uint32_t keyButtonR2;
    uint32_t keyButtonS1;
    uint32_t keyButtonS2;
    uint32_t keyButtonL3;
    uint32_t keyButtonR3;
    uint32_t keyButtonA1;
    uint32_t keyButtonA2;
    uint32_t keyButtonFn; 
} KeysConfig;

typedef struct
{
    int32_t         virtualPin;         // 虚拟pin
    GamepadHotkey   action;             // 快键功能
    bool            isHold;             // 是否长按
    bool            isLocked;           // 是否锁定
} GamepadHotkeyEntry;

typedef struct
{
    uint32_t    virtualPin;             // 虚拟pin
    float_t     maxDistance;            // 最大行程 单位毫米
} ADCButton;

typedef struct
{
    uint32_t        virtualPin;
} GPIOButton;


typedef struct __attribute__((packed))
{
    uint32_t   virtualPin;
    float_t    pressAccuracy;          // 按下精度 单位毫米
    float_t    releaseAccuracy;        // 回弹精度 单位毫米
    float_t    topDeadzone;            // 顶部死区 单位毫米
    float_t    bottomDeadzone;         // 底部死区 单位毫米
} RapidTriggerProfile;

typedef struct
{
    bool isAllBtnsConfiguring;
    ADCButtonDebounceAlgorithm debounceAlgorithm;
    RapidTriggerProfile triggerConfigs[NUM_ADC_BUTTONS];
} TriggerConfigs;

typedef struct
{
    bool ledEnabled;
    LEDEffect ledEffect;   
    uint32_t ledColor1;    // 0x000000-0xFFFFFF
    uint32_t ledColor2;    // 0x000000-0xFFFFFF
    uint32_t ledColor3;    // 0x000000-0xFFFFFF
    uint8_t ledBrightness; // 0-100
    uint8_t ledAnimationSpeed;      // 1-5
} LEDProfile;

typedef struct
{
    char id[16];
    char name[24];
    bool enabled;
    KeysConfig keysConfig;
    TriggerConfigs triggerConfigs;
    LEDProfile ledsConfigs;
} GamepadProfile;

typedef struct
{
    uint32_t version;
    BootMode bootMode;
    InputMode inputMode;
    char defaultProfileId[16];
    uint8_t numProfilesMax;
    GamepadProfile profiles[NUM_PROFILES];
    GamepadHotkeyEntry hotkeys[NUM_GAMEPAD_HOTKEYS];
    bool autoCalibrationEnabled;
} Config;

namespace ConfigUtils {
    bool load(Config& config);
    bool save(Config& config);
    bool reset(Config& config);
    bool fromStorage(Config& config);
    void makeDefaultProfile(GamepadProfile& profile, const char* id, bool isEnabled);
};

#ifdef __cplusplus
}
#endif

#endif