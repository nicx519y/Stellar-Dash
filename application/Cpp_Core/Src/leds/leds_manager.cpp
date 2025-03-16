#include "leds/leds_manager.hpp"
#include <algorithm>
LEDsManager::LEDsManager()
{
    opts = &STORAGE_MANAGER.getDefaultGamepadProfile()->ledsConfigs;
};

void LEDsManager::setup()
{

    WS2812B_Init();
    WS2812B_Start();

    if(!opts->ledEnabled) {
        WS2812B_SetAllLEDBrightness(0);
        WS2812B_SetAllLEDColor(0, 0, 0);
    } else {
        // 设置初始亮度
        brightness = (uint8_t)((float_t)(opts->ledBrightness) * LEDS_BRIGHTNESS_RATIO * 255.0 / 100.0); // 转换百分比到0-255
        WS2812B_SetAllLEDBrightness(brightness);

        // 转换颜色
        frontColor = hexToRGB(opts->ledColor1);
        backgroundColor1 = hexToRGB(opts->ledColor2);
        backgroundColor2 = hexToRGB(opts->ledColor3);

        // 根据效果设置初始状态
        switch(opts->ledEffect) {
            case LEDEffect::BREATHING:
                // 设置渐变：从 backgroundColor1 到 backgroundColor2
                // 亮度从 brightness 渐变到 0 再到 brightness
                gtc.setup(backgroundColor1, backgroundColor2, brightness, 0, LEDS_ANIMATION_CYCLE);
                break;
                
            case LEDEffect::STATIC:
            default:
                // 静态效果使用 backgroundColor1
                WS2812B_SetAllLEDColor(backgroundColor1.r, backgroundColor1.g, backgroundColor1.b);
                break;
        }
    }
}

/**
 * @brief 更新LED效果
 * @param virtualPinMask 按钮虚拟引脚掩码 表示按钮状态
 */
void LEDsManager::loop(uint32_t virtualPinMask)
{
    if(!opts->ledEnabled) {
        return;
    }

    // 声明变量在 switch 外部
    GradientState currentState;

    // 处理不同的灯效
    switch(opts->ledEffect) {
        case LEDEffect::BREATHING:
            // 更新渐变过程
            
            // 获取当前颜色和亮度
            currentState = gtc.getCurrentState();

            // 根据按钮状态设置颜色和亮度
            WS2812B_SetLEDColorByMask(
                frontColor,
                currentState.color,
                virtualPinMask
            );
            WS2812B_SetLEDBrightnessByMask(
                brightness,
                currentState.brightness,
                virtualPinMask
            );
            break;
            
        case LEDEffect::STATIC:
            // 静态效果不需要在循环中更新
            WS2812B_SetLEDColorByMask(
                frontColor,
                backgroundColor1,
                virtualPinMask
            );
            WS2812B_SetLEDBrightnessByMask(
                brightness,
                brightness,
                virtualPinMask
            );
            break;
    }
}

void LEDsManager::deinit()
{
    HAL_Delay(50); // 等待最后一帧发送完成
    WS2812B_Stop();
}

void LEDsManager::effectStyleNext() {
    opts->ledEffect = static_cast<LEDEffect>((opts->ledEffect + 1) % LEDEffect::NUM_EFFECTS);
    STORAGE_MANAGER.saveConfig();
    deinit();
    setup();
}

void LEDsManager::effectStylePrev() {
    opts->ledEffect = static_cast<LEDEffect>((opts->ledEffect - 1 + LEDEffect::NUM_EFFECTS) % LEDEffect::NUM_EFFECTS);
    STORAGE_MANAGER.saveConfig();
    deinit();
    setup();
}   

void LEDsManager::brightnessUp() {
    if(opts->ledBrightness == 100) {
        return;
    } else {
        opts->ledBrightness = std::min((int)opts->ledBrightness + 10, 100); // 增加10%
        STORAGE_MANAGER.saveConfig();
        brightness = (uint8_t)((float_t)(opts->ledBrightness) * LEDS_BRIGHTNESS_RATIO * 255.0 / 100.0);
        WS2812B_SetAllLEDBrightness(brightness);
    }
}

void LEDsManager::brightnessDown() {
    if(opts->ledBrightness == 0) {
        return;
    } else {
        opts->ledBrightness = std::max((int)opts->ledBrightness - 10, 0); // 减少10%
        STORAGE_MANAGER.saveConfig();
        brightness = (uint8_t)((float_t)(opts->ledBrightness) * LEDS_BRIGHTNESS_RATIO * 255.0 / 100.0);
        WS2812B_SetAllLEDBrightness(brightness);
    }
}

void LEDsManager::enableSwitch() {
    opts->ledEnabled = !opts->ledEnabled;
    STORAGE_MANAGER.saveConfig();
    deinit();
    setup();
}



