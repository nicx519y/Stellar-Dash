#include "leds/led_strip_controller.hpp"

#include "board_power.hpp"
#include "leds/led_config_safety.hpp"

namespace {

const LedStripDescriptor kKeyDescriptor = {
    WS2812B_STRIP_KEYS,
    static_cast<uint16_t>(NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS),
    WS2812B_KEYS_TIM_CHANNEL,
    WS2812B_KEYS_TIM_DMA_INSTANCE,
    LED_EN_PORT,
    LED_EN_PIN,
    LedConfigSafety::kKeyMaxHardwareDrivePercent,
};

const LedStripDescriptor kAmbientDescriptor = {
    WS2812B_STRIP_AMBIENT,
    static_cast<uint16_t>(NUM_LED_AROUND),
    WS2812B_AMBIENT_TIM_CHANNEL,
    WS2812B_AMBIENT_TIM_DMA_INSTANCE,
    AMBIENT_EN_PORT,
    AMBIENT_EN_PIN,
    LedConfigSafety::kAmbientMaxHardwareDrivePercent,
};

const LedStripController kKeyStrip(kKeyDescriptor);
const LedStripController kAmbientStrip(kAmbientDescriptor);

} // namespace

void LedStripController::init() const
{
    WS2812B_InitStrip(descriptor_.strip);
}

WS2812B_StateTypeDef LedStripController::start() const
{
    return WS2812B_StartStrip(descriptor_.strip);
}

WS2812B_StateTypeDef LedStripController::stop() const
{
    return WS2812B_StopStrip(descriptor_.strip);
}

WS2812B_StateTypeDef LedStripController::state() const
{
    return WS2812B_GetStateStrip(descriptor_.strip);
}

void LedStripController::setPowerEnabled(bool enabled) const
{
    if (descriptor_.strip == WS2812B_STRIP_AMBIENT) {
        BoardPower_SetAmbientLedEnabled(enabled);
    } else {
        BoardPower_SetKeyLedEnabled(enabled);
    }
}

void LedStripController::setAllColor(uint8_t r, uint8_t g, uint8_t b) const
{
    WS2812B_SetAllLEDColorStrip(descriptor_.strip, r, g, b);
}

void LedStripController::setColor(uint16_t index, uint8_t r, uint8_t g,
                                  uint8_t b) const
{
    WS2812B_SetLEDColorStrip(descriptor_.strip, r, g, b, index);
}

void LedStripController::setAllBrightness(uint8_t brightness) const
{
    WS2812B_SetAllLEDBrightnessStrip(descriptor_.strip, brightness);
}

void LedStripController::setBrightness(uint16_t index, uint16_t length,
                                       uint8_t brightness) const
{
    WS2812B_SetLEDBrightnessStrip(
        descriptor_.strip, brightness, index, length);
}

bool LedStripController::submitFrame() const
{
    return WS2812B_SubmitStrip(descriptor_.strip);
}

void LedStripController::updateStats(uint32_t* halfCount,
                                     uint32_t* completeCount) const
{
    WS2812B_GetUpdateStats(descriptor_.strip, halfCount, completeCount);
}

const LedStripController& LedStripController::keys()
{
    return kKeyStrip;
}

const LedStripController& LedStripController::ambient()
{
    return kAmbientStrip;
}
