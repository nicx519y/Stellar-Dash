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
#include "system_logger.h"

void InputState::setup() {
    LOG_INFO("INPUT", "Starting input state setup");
    APP_DBG("InputState::setup");
    
    /**************** 初始化USB end ******************* */

    InputMode inputMode = STORAGE_MANAGER.getInputMode();
    // InputMode inputMode = InputMode::INPUT_MODE_PS5; // TODO: 需要根据实际情况修改 
    // InputMode inputMode = InputMode::INPUT_MODE_XINPUT;
    LOG_INFO("INPUT", "Selected input mode: %d", static_cast<int>(inputMode));
    APP_DBG("InputState::setup inputMode: %d", inputMode);
    
    if(inputMode == InputMode::INPUT_MODE_CONFIG) {
        LOG_ERROR("INPUT", "Invalid input mode CONFIG for input state");
        APP_ERR("InputState::setup error - inputMode: INPUT_MODE_CONFIG, not supported for input state");
        return;
    }
    
    LOG_DEBUG("INPUT", "Initializing driver manager");
    DRIVER_MANAGER.setup(inputMode);
    inputDriver = DRIVER_MANAGER.getDriver();
    if ( inputDriver != nullptr ) {
		inputDriver->initializeAux();
        LOG_DEBUG("INPUT", "Input driver auxiliary initialization completed");
        APP_DBG("InputState::setup inputDriver->initializeAux() done");
		// Check if we have a USB listener
		USBListener * listener = inputDriver->get_usb_auth_listener();
		if (listener != nullptr) {
			LOG_DEBUG("INPUT", "USB auth listener found, registering with host manager");
			APP_DBG("InputState::setup listener: %p", listener);
			USB_HOST_MANAGER.pushListener(listener);
		}
	} else {
        LOG_ERROR("INPUT", "Failed to get input driver instance");
    }

    // 初始化USB主机
    LOG_DEBUG("INPUT", "Starting USB host manager");
    USB_HOST_MANAGER.start();

    // 初始化TinyUSB设备栈
    APP_DBG("tud_init start");  
    tud_init(TUD_OPT_RHPORT);
    APP_DBG("tud_init done");
    LOG_DEBUG("INPUT", "TinyUSB device stack initialized");

    ADC_BTNS_WORKER.setup();
    GPIO_BTNS_WORKER.setup();
    GAMEPAD.setup();

    #if HAS_LED == 1
    LOG_DEBUG("INPUT", "Initializing LED manager");
    LEDS_MANAGER.setup();
    #endif

    isRunning = true;
    LOG_INFO("INPUT", "Input state setup completed successfully");

    Logger_Flush();
}

void InputState::loop() { 

    virtualPinMask = GPIO_BTNS_WORKER.read() | ADC_BTNS_WORKER.read();

    // 只有在没有按下FN键时才处理游戏手柄数据
    if((virtualPinMask & FN_BUTTON_VIRTUAL_PIN) == 0) {
        GAMEPAD.read(virtualPinMask);
        inputDriver->process(&GAMEPAD);  // 处理游戏手柄数据，将按键数据映射到xinput协议 形成 report 数据，然后通过 usb 发送出去
    } else {
        // 更新热键状态，处理hold和click逻辑
        HOTKEYS_MANAGER.updateHotkeyState(virtualPinMask, lastVirtualPinMask);
    }

    lastVirtualPinMask = virtualPinMask;

    // 处理USB任务
    tud_task(); // 设备模式任务
    USB_HOST_MANAGER.process();
    inputDriver->processAux();

    #if HAS_LED == 1
    LEDS_MANAGER.loop(virtualPinMask);
    #endif
}

void InputState::reset() {
    // 清除FN键状态标志
    static bool fnPressedLogged = false;
    fnPressedLogged = false;
    LOG_DEBUG("INPUT", "Input state reset completed");
}
