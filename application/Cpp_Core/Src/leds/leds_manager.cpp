#include "leds/leds_manager.hpp"
#include <algorithm>
#include "board_cfg.h"

#define LEDS_ANIMATION_CYCLE 10000  // 10秒周期

LEDsManager::LEDsManager()
{
    opts = &STORAGE_MANAGER.getDefaultGamepadProfile()->ledsConfigs;
    animationStartTime = 0;
    lastButtonState = 0;
    rippleCount = 0;
    for (int i = 0; i < 5; i++) {
        ripples[i].centerIndex = 0;
        ripples[i].startTime = 0;
    }
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
        setBrightness(opts->ledBrightness);

        // 转换颜色
        frontColor = hexToRGB(opts->ledColor1);
        backgroundColor1 = hexToRGB(opts->ledColor2);
        backgroundColor2 = hexToRGB(opts->ledColor3);
        defaultBackColor = {0, 0, 0}; // 黑色作为默认背景色

        // 初始化动画
        animationStartTime = HAL_GetTick();
        
        frontColor.r = 255;
        frontColor.g = 0;
        frontColor.b = 0;

        backgroundColor1.r = 0;
        backgroundColor1.g = 255;
        backgroundColor1.b = 0;

        backgroundColor2.r = 0;
        backgroundColor2.g = 0;
        backgroundColor2.b = 255;

        brightness = 50;

        // 对于静态效果，直接设置颜色
        if (opts->ledEffect == LEDEffect::STATIC) {
            WS2812B_SetAllLEDColor(backgroundColor1.r, backgroundColor1.g, backgroundColor1.b);
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

    // 处理按钮按下事件（用于涟漪效果）
    processButtonPress(virtualPinMask);
    
    // 更新涟漪状态
    updateRipples();
    
    // 获取动画进度
    float progress = getAnimationProgress();
    
    // 获取当前动画算法
    LedAnimationAlgorithm algorithm = getLedAnimation(opts->ledEffect);
    
    // 准备全局动画参数
    LedAnimationParams params;
    params.colorEnabled = true;
    params.frontColor = frontColor;
    params.backColor1 = backgroundColor1;
    params.backColor2 = backgroundColor2;
    params.defaultBackColor = defaultBackColor;
    params.effectStyle = opts->ledEffect;
    params.brightness = brightness;
    params.progress = progress;
    
    // 设置涟漪参数
    params.global.rippleCount = rippleCount;
    for (uint8_t i = 0; i < rippleCount && i < 5; i++) {
        params.global.rippleCenters[i] = ripples[i].centerIndex;
        uint32_t now = HAL_GetTick();
        uint32_t elapsed = now - ripples[i].startTime;
        const uint32_t rippleDuration = 3000; // 3秒
        params.global.rippleProgress[i] = (float)elapsed / rippleDuration;
        if (params.global.rippleProgress[i] > 1.0f) {
            params.global.rippleProgress[i] = 1.0f;
        }
    }
    
    // 为每个LED计算颜色并设置
    for (uint8_t i = 0; i < NUM_LED; i++) {
        params.index = i;
        params.pressed = (virtualPinMask & (1 << i)) != 0;
        
        RGBColor color = algorithm(params);
        WS2812B_SetLEDColor(color.r, color.g, color.b, i);
    }
}

void LEDsManager::processButtonPress(uint32_t virtualPinMask)
{
    // 检测新按下的按钮（用于涟漪效果）
    uint32_t newPressed = virtualPinMask & ~lastButtonState;
    
    if (newPressed != 0 && opts->ledEffect == LEDEffect::RIPPLE) {
        // 查找按下的按钮并添加涟漪
        for (uint8_t i = 0; i < NUM_LED; i++) {
            if (newPressed & (1 << i)) {
                // 添加新的涟漪
                if (rippleCount < 5) {
                    ripples[rippleCount].centerIndex = i;
                    ripples[rippleCount].startTime = HAL_GetTick();
                    rippleCount++;
                } else {
                    // 如果已满，替换最老的涟漪
                    ripples[0].centerIndex = i;
                    ripples[0].startTime = HAL_GetTick();
                }
                break;
            }
        }
    }
    
    lastButtonState = virtualPinMask;
}

void LEDsManager::updateRipples()
{
    if (opts->ledEffect != LEDEffect::RIPPLE) {
        rippleCount = 0;
        return;
    }
    
    uint32_t now = HAL_GetTick();
    const uint32_t rippleDuration = 3000; // 3秒
    
    // 移除过期的涟漪
    uint8_t newCount = 0;
    for (uint8_t i = 0; i < rippleCount; i++) {
        if ((now - ripples[i].startTime) < rippleDuration) {
            if (newCount != i) {
                ripples[newCount] = ripples[i];
            }
            newCount++;
        }
    }
    rippleCount = newCount;
}

float LEDsManager::getAnimationProgress()
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - animationStartTime;
    float progress = (float)(elapsed % LEDS_ANIMATION_CYCLE) / LEDS_ANIMATION_CYCLE;
    return progress;
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
        setBrightness(opts->ledBrightness);
    }
}

void LEDsManager::brightnessDown() {
    if(opts->ledBrightness == 0) {
        return;
    } else {
        opts->ledBrightness = std::max((int)opts->ledBrightness - 10, 0); // 减少10%
        STORAGE_MANAGER.saveConfig();
        setBrightness(opts->ledBrightness);
    }
}

void LEDsManager::enableSwitch() {
    opts->ledEnabled = !opts->ledEnabled;
    STORAGE_MANAGER.saveConfig();
    deinit();
    setup();
}

void LEDsManager::setBrightness(uint8_t brightness) {
    this->brightness = (uint8_t)((float_t)(brightness) * LEDS_BRIGHTNESS_RATIO * 255.0 / 100.0);
    WS2812B_SetAllLEDBrightness(brightness);
}

/**
 * @brief 测试特定的LED动画效果
 * @param effect 要测试的动画效果
 * @param progress 动画进度 (0.0-1.0)
 * @param buttonMask 模拟的按钮状态掩码
 */
void LEDsManager::testAnimation(LEDEffect effect, float progress, uint32_t buttonMask)
{
    if(!opts->ledEnabled) {
        APP_DBG("LEDsManager::testAnimation - LED disabled");
        return;
    }

    // 获取测试动画算法
    LedAnimationAlgorithm algorithm = getLedAnimation(effect);
    
    // 准备测试参数
    LedAnimationParams params;
    params.colorEnabled = true;
    params.frontColor = frontColor;
    params.backColor1 = backgroundColor1;
    params.backColor2 = backgroundColor2;
    params.defaultBackColor = defaultBackColor;
    params.effectStyle = effect;
    params.brightness = brightness;
    params.progress = progress;
    
    // 如果是涟漪效果，设置一个测试涟漪
    if (effect == LEDEffect::RIPPLE) {
        params.global.rippleCount = 1;
        params.global.rippleCenters[0] = 10; // 中心按钮索引10
        params.global.rippleProgress[0] = progress;
    } else {
        params.global.rippleCount = 0;
    }
    
    // 为每个LED计算颜色并设置（只调试前3个LED避免过多输出）
    for (uint8_t i = 0; i < NUM_LED; i++) {
        params.index = i;
        params.pressed = (buttonMask & (1 << i)) != 0;
        
        RGBColor color = algorithm(params);
        
        WS2812B_SetLEDColor(color.r, color.g, color.b, i);
    }
}

/**
 * @brief 预览动画效果的完整周期
 * @param effect 要预览的动画效果
 * @param duration 预览持续时间(毫秒)
 */
void LEDsManager::previewAnimation(LEDEffect effect, uint32_t duration)
{
    if(!opts->ledEnabled) {
        return;
    }
    
    uint32_t startTime = HAL_GetTick();
    uint32_t buttonMask = 0; // 可以设置一些按钮状态用于测试
    
    // 为涟漪效果设置一些测试按钮
    if (effect == LEDEffect::RIPPLE) {
        buttonMask = 0x04; // 按钮2按下，用于触发涟漪
    }
    
    while ((HAL_GetTick() - startTime) < duration) {
        uint32_t elapsed = HAL_GetTick() - startTime;
        float progress = (float)(elapsed % LEDS_ANIMATION_CYCLE) / LEDS_ANIMATION_CYCLE; // 使用相同的10秒周期
        
        testAnimation(effect, progress, buttonMask);
        HAL_Delay(16); // ~60FPS
        
        // 为涟漪效果在中途改变按钮状态
        if (effect == LEDEffect::RIPPLE && elapsed > duration / 3 && elapsed < duration / 3 + 100) {
            buttonMask = 0x100; // 按钮8按下，创建新的涟漪
        } else if (effect == LEDEffect::RIPPLE && elapsed > duration / 3 + 100) {
            buttonMask = 0; // 释放按钮
        }
    }
}




