#include "input_state.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "gamepad.hpp"
#include "leds/leds_manager.hpp"
#include "hotkeys_manager.hpp"

void InputState::setup() {

    APP_DBG("InputState::setup");

    InputMode inputMode = InputMode::INPUT_MODE_XINPUT;
    DRIVER_MANAGER.setup(inputMode);
    inputDriver = DRIVER_MANAGER.getDriver();

    tud_init(TUD_OPT_RHPORT); // 初始化TinyUSB
    ADC_BTNS_WORKER.setup();
    GPIO_BTNS_WORKER.setup();
    GAMEPAD.setup();

    #if HAS_LED == 1
    LEDS_MANAGER.setup();
    #endif

    workTime = MICROS_TIMER.micros();
    calibrationTime = MICROS_TIMER.micros();
    ledAnimationTime = MICROS_TIMER.micros();

    isRunning = true;
}

void InputState::loop() { 

    if(MICROS_TIMER.checkInterval(READ_BTNS_INTERVAL, workTime)) {


        // ADC_MANAGER.ADCValuesTestPrint();
        // HAL_Delay(1); // 延迟100ms，避免异常

        virtualPinMask = GPIO_BTNS_WORKER.read() | ADC_BTNS_WORKER.read();

        if(((lastVirtualPinMask & virtualPinMask) == FN_BUTTON_VIRTUAL_PIN) && (lastVirtualPinMask != FN_BUTTON_VIRTUAL_PIN)) { // 如果按下了 FN 键并且不只是 FN 键，并且click了其他键，则执行快捷键
            HOTKEYS_MANAGER.runVirtualPinMask(lastVirtualPinMask);
        } else { // 否则，处理游戏手柄数据
            GAMEPAD.read(virtualPinMask);
            inputDriver->process(&GAMEPAD);  // xinput 处理游戏手柄数据，将按键数据映射到xinput协议 形成 report 数据，然后通过 usb 发送出去
        }

        lastVirtualPinMask = virtualPinMask;
    }

    tud_task();

    #if HAS_LED == 1
    if(MICROS_TIMER.checkInterval(LEDS_ANIMATION_INTERVAL, ledAnimationTime)) {
        LEDS_MANAGER.loop(virtualPinMask);
    }
    #endif

    #if ENABLED_DYNAMIC_CALIBRATION == 1
    if(MICROS_TIMER.checkInterval(DYNAMIC_CALIBRATION_INTERVAL, calibrationTime)) {
        ADC_BTNS_WORKER.dynamicCalibration();
    }
    #endif
}

void InputState::reset() {

}
