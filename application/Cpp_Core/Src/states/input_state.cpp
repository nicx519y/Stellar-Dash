#include "input_state.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "gamepad.hpp"
#include "leds/leds_manager.hpp"
#include "hotkeys_manager.hpp"
#include "usb.h"
#include "usbh.h"
#include "usb_host_monitor.h"
#include "usblistener.hpp"
#include "usbhostmanager.hpp"
#include "gpdriver.hpp"

void InputState::setup() {
    APP_DBG("InputState::setup");
    
    /**************** 初始化USB end ******************* */

    InputMode inputMode = STORAGE_MANAGER.getInputMode();
    // InputMode inputMode = InputMode::INPUT_MODE_PS5; // TODO: 需要根据实际情况修改 
    // InputMode inputMode = InputMode::INPUT_MODE_XINPUT;
    APP_DBG("InputState::setup inputMode: %d", inputMode);
    if(inputMode == InputMode::INPUT_MODE_CONFIG) {
        APP_ERR("InputState::setup error - inputMode: INPUT_MODE_CONFIG, not supported for input state");
        return;
    }
    
    DRIVER_MANAGER.setup(inputMode);
    inputDriver = DRIVER_MANAGER.getDriver();
    if ( inputDriver != nullptr ) {
		inputDriver->initializeAux();
        APP_DBG("InputState::setup inputDriver->initializeAux() done");
		// Check if we have a USB listener
		USBListener * listener = inputDriver->get_usb_auth_listener();
		if (listener != nullptr) {
			APP_DBG("InputState::setup listener: %p", listener);
			USB_HOST_MANAGER.pushListener(listener);
		}
	}

    // 初始化USB主机
    USB_HOST_MANAGER.start();

    // 初始化TinyUSB设备栈
    APP_DBG("tud_init start");  
    tud_init(TUD_OPT_RHPORT);
    APP_DBG("tud_init done");

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

        virtualPinMask = GPIO_BTNS_WORKER.read() | ADC_BTNS_WORKER.read();

        if(((lastVirtualPinMask & virtualPinMask) == FN_BUTTON_VIRTUAL_PIN) && (lastVirtualPinMask != FN_BUTTON_VIRTUAL_PIN)) { // 如果按下了 FN 键并且不只是 FN 键，并且click了其他键，则执行快捷键
            HOTKEYS_MANAGER.runVirtualPinMask(lastVirtualPinMask);
        } else if((virtualPinMask & FN_BUTTON_VIRTUAL_PIN) == 0) { // 如果按下了 FN 键，则不处理游戏手柄数据
            GAMEPAD.read(virtualPinMask);
            inputDriver->process(&GAMEPAD);  // 处理游戏手柄数据，将按键数据映射到xinput协议 形成 report 数据，然后通过 usb 发送出去
        }

        lastVirtualPinMask = virtualPinMask;
    }


    // 处理USB任务
    tud_task(); // 设备模式任务
    USB_HOST_MANAGER.process();
    inputDriver->processAux();

    #if HAS_LED == 1
    if(MICROS_TIMER.checkInterval(LEDS_ANIMATION_INTERVAL, ledAnimationTime)) {
        LEDS_MANAGER.loop(virtualPinMask);
    }
    #endif

    if(STORAGE_MANAGER.config.autoCalibrationEnabled) {
        if(MICROS_TIMER.checkInterval(DYNAMIC_CALIBRATION_INTERVAL, calibrationTime)) {
            ADC_BTNS_WORKER.dynamicCalibration();
        }
    }
}

void InputState::reset() {

}
