#ifndef HBOX_LED_STRIP_CONTROLLER_HPP
#define HBOX_LED_STRIP_CONTROLLER_HPP

#include <stdint.h>

#include "board_cfg.h"
#include "pwm-ws2812b.h"

struct LedStripDescriptor {
    WS2812B_Strip strip;
    uint16_t ledCount;
    uint32_t timerChannel;
    DMA_Stream_TypeDef* dmaStream;
    GPIO_TypeDef* enablePort;
    uint16_t enablePin;
    uint8_t maxDrivePercent;
};

class LedStripController {
public:
    explicit constexpr LedStripController(const LedStripDescriptor& descriptor)
        : descriptor_(descriptor) {}

    const LedStripDescriptor& descriptor() const { return descriptor_; }
    void init() const;
    WS2812B_StateTypeDef start() const;
    WS2812B_StateTypeDef stop() const;
    WS2812B_StateTypeDef state() const;
    void setPowerEnabled(bool enabled) const;
    void setAllColor(uint8_t r, uint8_t g, uint8_t b) const;
    void setColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b) const;
    void setAllBrightness(uint8_t brightness) const;
    void setBrightness(uint16_t index, uint16_t length,
                       uint8_t brightness) const;
    bool submitFrame() const;
    void updateStats(uint32_t* halfCount, uint32_t* completeCount) const;

    static const LedStripController& keys();
    static const LedStripController& ambient();

private:
    const LedStripDescriptor& descriptor_;
};

#endif
