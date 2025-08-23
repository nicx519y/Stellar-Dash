#ifndef CONFIG_H
#define CONFIG_H

#include <string.h>
#include <map>
#include "enums.hpp"
#include "stm32h750xx.h"
#include "stm32h7xx_hal.h"
#include "board_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t gameControllerButtonMask; // 游戏控制器按钮掩码 0001 0000 0010 0000 0000 0000 0000 0000  说明包含哪些游戏控制器按钮
    uint32_t virtualPinMask;           // 虚拟引脚掩码 0001 0000 0010 0000 0000 0000 0000 0000  说明包含哪些虚拟引脚
} KeyCombination;

typedef struct
{
    SOCDMode socdMode;
    bool fourWayMode;
    bool invertXAxis;
    bool invertYAxis;
    bool keysEnableTag[NUM_ADC_BUTTONS]; // 0-15 表示第0-15个按钮 1表示启用 0表示禁用
    // 将 std::map 替换为固定大小的数组
    uint32_t keyMapping[NUM_GAME_CONTROLLER_BUTTONS]; // 按键映射，20个按钮类型，每个4字节
    KeyCombination keyCombinations[MAX_KEY_COMBINATION];   // 按键组合键配置 说明 哪些游戏控制器按键组合键对应哪些物理按键
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

    bool aroundLedEnabled;       // 是否启用环绕灯效
    bool aroundLedSyncToMainLed; // 是否同步主灯效
    bool aroundLedTriggerByButton; // 是否由按钮触发环绕灯动画
    AroundLEDEffect aroundLedEffect; // 环绕灯效
    uint32_t aroundLedColor1;    // 0x000000-0xFFFFFF
    uint32_t aroundLedColor2;    // 0x000000-0xFFFFFF
    uint32_t aroundLedColor3;    // 0x000000-0xFFFFFF
    uint8_t aroundLedBrightness; // 0-100
    uint8_t aroundLedAnimationSpeed; // 1-5
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