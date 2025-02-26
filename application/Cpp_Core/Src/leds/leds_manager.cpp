#include "leds/leds_manager.hpp"

LEDsManager::LEDsManager()
{
    opts = &STORAGE_MANAGER.getDefaultGamepadProfile()->ledsConfigs;
};

void LEDsManager::setup()
{
    if(!opts->ledEnabled) {
        WS2812B_Stop();
        return;
    }

    WS2812B_Init();
    WS2812B_Start();

    // 设置初始亮度
    brightness = (uint8_t)(opts->ledBrightness * LEDS_BRIGHTNESS_RADIO * 255 / 100); // 转换百分比到0-255
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

