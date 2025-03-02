#include "main_state_machine.hpp"

MainStateMachine::MainStateMachine() 
    : gamepad(Gamepad::getInstance())
    , storage(Storage::getInstance())
    , state(WebConfigState::getInstance())
{

}

void MainStateMachine::setup()
{
    APP_DBG("MainStateMachine::setup");
    Storage::getInstance().initConfig();
    APP_DBG("Storage initConfig success.");

    // BootMode bootMode = Storage::getInstance().config.bootMode;
    BootMode bootMode = BootMode::BOOT_MODE_WEB_CONFIG;
    // BootMode bootMode = BootMode::BOOT_MODE_INPUT;
    APP_DBG("BootMode: %d", bootMode);

    workTime = MICROS_TIMER.micros();

    switch(bootMode) {
        case BootMode::BOOT_MODE_WEB_CONFIG:
        
            state = WEB_CONFIG_STATE;
            state.setup();

            while(1) {
                state.loop();
            }

            break; 
        case BootMode::BOOT_MODE_INPUT:

            /*** 初始化ADC按钮 & LED test begin ***/
            
            ADC_BTNS_WORKER.setup();
            GPIO_BTNS_WORKER.setup();
            GAMEPAD.setup();

            #if HAS_LED == 1
                LEDS_MANAGER.setup();
            #endif

            workTime = MICROS_TIMER.micros();
            calibrationTime = MICROS_TIMER.micros();
            ledAnimationTime = MICROS_TIMER.micros();

            while(1) {
                if(MICROS_TIMER.checkInterval(READ_BTNS_INTERVAL, workTime)) {
                    virtualPinMask = GPIO_BTNS_WORKER.read() | ADC_BTNS_WORKER.read();
                    GAMEPAD.read(virtualPinMask);
                    // driver.process(GAMEPAD); // xinput 处理游戏手柄数据，将按键数据映射到xinput协议 形成 report 数据，然后通过 usb 发送出去
                }

                tud_task();

                #if ENABLED_DYNAMIC_CALIBRATION == 1
                if(MICROS_TIMER.checkInterval(DYNAMIC_CALIBRATION_INTERVAL, calibrationTime)) {
                    ADC_BTNS_WORKER.dynamicCalibration();
                }
                #endif

                #if HAS_LED == 1
                if(MICROS_TIMER.checkInterval(LEDS_ANIMATION_INTERVAL * 1000, workTime)) {
                    LEDS_MANAGER.loop(virtualPinMask);
                }
                #endif

            }

            break;



    }
}
