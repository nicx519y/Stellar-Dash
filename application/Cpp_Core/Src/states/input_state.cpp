#include "input_state.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "gamepad.hpp"
#include "leds/leds_manager.hpp"
#include "hotkeys_manager.hpp"
#include "usb.h" // 包含USB相关函数

void InputState::setup() {

    APP_DBG("InputState::setup");

    InputMode inputMode = STORAGE_MANAGER.getInputMode();

    if(inputMode == InputMode::INPUT_MODE_CONFIG) {
        APP_ERR("InputState::setup error - inputMode: INPUT_MODE_CONFIG, not supported for input state");
        return;
    }

    DRIVER_MANAGER.setup(inputMode);
    inputDriver = DRIVER_MANAGER.getDriver();

    APP_DBG("tusb init start. ");

    tud_init(TUD_OPT_RHPORT); // 初始化TinyUSB

    APP_DBG("tud_init done. ");

    tuh_init(TUH_OPT_RHPORT); // 初始化TinyUSB Host

    APP_DBG("tuh_init done. ");

    ADC_BTNS_WORKER.setup();
    GPIO_BTNS_WORKER.setup();
    GAMEPAD.setup();

    #if HAS_LED == 1
    // LEDS_MANAGER.setup();
    #endif

    workTime = MICROS_TIMER.micros();
    calibrationTime = MICROS_TIMER.micros();
    ledAnimationTime = MICROS_TIMER.micros();

    isRunning = true;
}

void InputState::loop() { 

    // if(MICROS_TIMER.checkInterval(READ_BTNS_INTERVAL, workTime)) {

    //     virtualPinMask = GPIO_BTNS_WORKER.read() | ADC_BTNS_WORKER.read();

    //     if(((lastVirtualPinMask & virtualPinMask) == FN_BUTTON_VIRTUAL_PIN) && (lastVirtualPinMask != FN_BUTTON_VIRTUAL_PIN)) { // 如果按下了 FN 键并且不只是 FN 键，并且click了其他键，则执行快捷键
    //         HOTKEYS_MANAGER.runVirtualPinMask(lastVirtualPinMask);
    //     } else { // 否则，处理游戏手柄数据
    //         GAMEPAD.read(virtualPinMask);
    //         inputDriver->process(&GAMEPAD);  // xinput 处理游戏手柄数据，将按键数据映射到xinput协议 形成 report 数据，然后通过 usb 发送出去
    //     }

    //     lastVirtualPinMask = virtualPinMask;
    // }

    tud_task();
    tuh_task();

    // 检查是否有设备连接到USB主机端口
    static uint32_t lastCheck = 0;
    if (HAL_GetTick() - lastCheck > 200) {
        lastCheck = HAL_GetTick();
        
        // 检查是否有设备连接到USB主机端口
        if (tuh_mounted(1)) { // 检查设备地址1是否有连接
            APP_DBG("USB Host HID Device Connected");
            
            // 获取已连接的HID设备数量
            uint8_t hidCount = 0;
            for (uint8_t i = 1; i <= CFG_TUH_DEVICE_MAX; i++) {
                if (tuh_hid_mounted(i, 0)) hidCount++;
            }
            APP_DBG("USB Host HID Device Count: %d", hidCount);
        } else {
            APP_DBG("USB Host HID Device Disconnected");
        }
        
        // 调用详细的USB主机状态检查函数
        USB_HOST_StatusCheck();
    }

    #if HAS_LED == 1
    // if(MICROS_TIMER.checkInterval(LEDS_ANIMATION_INTERVAL, ledAnimationTime)) {
    //     LEDS_MANAGER.loop(virtualPinMask);
    // }
    #endif

    #if ENABLED_DYNAMIC_CALIBRATION == 1
    // if(MICROS_TIMER.checkInterval(DYNAMIC_CALIBRATION_INTERVAL, calibrationTime)) {
    //     ADC_BTNS_WORKER.dynamicCalibration();
    // }
    #endif
}

void InputState::reset() {

}
