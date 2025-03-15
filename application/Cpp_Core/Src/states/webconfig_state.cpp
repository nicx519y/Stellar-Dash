#include "states/webconfig_state.hpp"
#include "adc_btns/adc_btns_marker.hpp"
#include "pwm-ws2812b.h"
#include "qspi-w25q64.h"

void WebConfigState::setup() {

    APP_DBG("WebConfigState::setup");

    // TODO: 初始化Web配置状态
    // 例如：
    // - 启动Web服务器
    // - 初始化配置参数
    // - 设置回调函数等
    InputMode inputMode = InputMode::INPUT_MODE_CONFIG;
    DRIVER_MANAGER.setup(inputMode);      
    ConfigType configType = ConfigType::CONFIG_TYPE_WEB;
    CONFIG_MANAGER.setup(configType);
    tud_init(TUD_OPT_RHPORT); // 初始化TinyUSB
    inputDriver = DRIVER_MANAGER.getDriver();

    // 设置QSPI为内存映射模式，方便访问Websources
    QSPI_W25Qxx_EnterMemoryMappedMode();

    isRunning = true;
}

void WebConfigState::loop() {
    if(isRunning) {
        CONFIG_MANAGER.loop();
    }
}

void WebConfigState::reset() {
    isRunning = false;
}
