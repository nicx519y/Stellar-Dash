#include "input_state.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "gamepad.hpp"
#include "leds/leds_manager.hpp"
#include "hotkeys_manager.hpp"
#include "usb.h" // 包含USB相关函数
#include "usbh.h"
#include "usb_host_monitor.h"

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx) {
    APP_DBG("tuh_hid_umount_cb");
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx, uint8_t const *report, uint16_t len) {
    APP_DBG("tuh_hid_mount_cb");
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx, uint8_t const *report, uint16_t len) {
    APP_DBG("tuh_hid_report_received_cb");
}

void InputState::setup() {
    APP_DBG("InputState::setup");
    
    InputMode inputMode = STORAGE_MANAGER.getInputMode();
    APP_DBG("InputState::setup inputMode: %d", inputMode);
    if(inputMode == InputMode::INPUT_MODE_CONFIG) {
        APP_ERR("InputState::setup error - inputMode: INPUT_MODE_CONFIG, not supported for input state");
        return;
    }
    
    DRIVER_MANAGER.setup(inputMode);
    inputDriver = DRIVER_MANAGER.getDriver();

    // ADC_BTNS_WORKER.setup();
    // GPIO_BTNS_WORKER.setup();
    // GAMEPAD.setup();

    // #if HAS_LED == 1
    // LEDS_MANAGER.setup();
    // #endif

    /*************** 初始化USB start ************ */
    APP_DBG("tuh_init start");
    tuh_init(TUH_OPT_RHPORT);
    usb_host_monitor_init();
    APP_DBG("tuh_init done");
    
    // 初始化TinyUSB设备栈
    APP_DBG("tud_init start");  
    tud_init(TUD_OPT_RHPORT);
    APP_DBG("tud_init done");
    
    
    /**************** 初始化USB end ******************* */

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


    // 处理USB任务
    tud_task(); // 设备模式任务
    tuh_task(); // 主机模式任务
    usb_host_monitor_task(); // 主机监控任务

    // #if HAS_LED == 1
    // if(MICROS_TIMER.checkInterval(LEDS_ANIMATION_INTERVAL, ledAnimationTime)) {
    //     LEDS_MANAGER.loop(virtualPinMask);
    // }
    // #endif

    #if ENABLED_DYNAMIC_CALIBRATION == 1
    // if(MICROS_TIMER.checkInterval(DYNAMIC_CALIBRATION_INTERVAL, calibrationTime)) {
    //     ADC_BTNS_WORKER.dynamicCalibration();
    // }
    #endif
}

void InputState::reset() {

}
