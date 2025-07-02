#include "states/webconfig_state.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "pwm-ws2812b.h"
#include "qspi-w25q64.h"
#include "configs/webconfig_btns_manager.hpp"
#include "configs/webconfig_leds_manager.hpp"
#include "leds/leds_manager.hpp"
#include "adc_btns/adc_manager.hpp"
#include "system_logger.h"

void WebConfigState::setup() {

    LOG_INFO("WEBCONFIG", "Starting web configuration state setup");
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
    LOG_DEBUG("WEBCONFIG", "Setting QSPI to memory mapped mode for web resources access");
    int8_t qspi_result = QSPI_W25Qxx_EnterMemoryMappedMode();
    if (qspi_result != 0) {
        LOG_ERROR("WEBCONFIG", "Failed to enter QSPI memory mapped mode, error: %d", qspi_result);
    } else {
        LOG_DEBUG("WEBCONFIG", "QSPI memory mapped mode enabled successfully");
    }

    // 初始化LED管理器
    LEDS_MANAGER.setup();

    isRunning = true;
    LOG_INFO("WEBCONFIG", "Web configuration state setup completed successfully");
}

void WebConfigState::loop() {
    if(isRunning) {
        ADC_CALIBRATION_MANAGER.processCalibration(); // 处理校准逻辑
        CONFIG_MANAGER.loop();
        
        // 实时更新按键状态并生成事件（在主循环中调用）
        WEBCONFIG_BTNS_MANAGER.update();
        
        // 获取当前按键状态用于LED预览效果
        std::vector<bool> buttonStates = WEBCONFIG_BTNS_MANAGER.getButtonStates();
        uint32_t buttonMask = 0;
        for (size_t i = 0; i < buttonStates.size() && i < 32; i++) {
            if (buttonStates[i]) {
                buttonMask |= (1U << i);
            }
        }
        
        // APP_DBG("WebConfigState::loop - buttonMask: 0x%08lX", buttonMask);

        // 更新LED预览效果
        WEBCONFIG_LEDS_MANAGER.update(buttonMask);
        
        // // 如果没有在预览模式，正常运行LED管理器
        // if (!WEBCONFIG_LEDS_MANAGER.isInPreviewMode()) {
        //     LEDS_MANAGER.loop(buttonMask);
        // }
    }
}

void WebConfigState::reset() {
    isRunning = false;
    
    // 清除LED预览模式，恢复默认配置
    WEBCONFIG_LEDS_MANAGER.clearPreviewConfig();
    
    LOG_DEBUG("WEBCONFIG", "Web configuration state reset completed");
}
