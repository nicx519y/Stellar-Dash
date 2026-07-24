#include "states/webconfig_state.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "pwm-ws2812b.h"
#include "qspi-w25q64.h"
#include "configs/webconfig_btns_manager.hpp"
#include "configs/webconfig_leds_manager.hpp"
#include "configs/websocket_command_handler.hpp"
#include "leds/leds_manager.hpp"
#include "adc_btns/adc_manager.hpp"
#include "system_logger.h"
#include "board_mode.hpp"
#include "board_power.hpp"
#include "ch585_role_bootstrap.hpp"
#include "rf_bridge_port.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "usb_board_link.hpp"
#include "usbdriver.hpp"
#include "webhid_service.hpp"

namespace {

static void enterWebSafeState()
{
    SPIScreenManager::getInstance().shutdown();
    BOARD_POWER.enterSafeState();
}

} // namespace

void WebConfigState::setup() {

    LOG_INFO("WEBCONFIG", "Starting web configuration state setup");
    APP_DBG("WebConfigState::setup");

    /*
     * WebConfig is permitted only in the physical USB switch position and
     * always uses the independent CH585 maintenance role.
     */
    if (!BOARD_MODE.isStable() || BOARD_MODE.current() != BoardMode::Usb) {
        enterWebSafeState();
        LOG_ERROR("WEBCONFIG", "Physical switch is not in USB position");
        return;
    }

    WEBHID_SERVICE.shutdown();
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    CH585_ROLE_BOOTSTRAP.setSelector(UsbBoardLink_SelectRoleCallback);
    if (!CH585_ROLE_BOOTSTRAP.start(Ch585Role::Maintenance) ||
        !USB_DRIVER.start(InputMode::INPUT_MODE_CONFIG)) {
        USB_DRIVER.shutdown();
        USB_BOARD_LINK.shutdown();
        CH585_ROLE_BOOTSTRAP.shutdown();
        enterWebSafeState();
        LOG_ERROR("WEBCONFIG", "CH585 maintenance capability gate failed");
        return;
    }
    /*
     * V2 WebConfig is hosted by the HTTPS server.  Do not start the legacy
     * RNDIS/NCM, LwIP, httpd or WebSocket stack here.  The command handlers
     * remain the single source of configuration semantics and are invoked by
     * the authenticated WebHID dispatcher.
     */
    WebSocketCommandManager::getInstance().initializeHandlers();
    if (!WEBHID_SERVICE.setup()) {
        WEBHID_SERVICE.shutdown();
        USB_DRIVER.shutdown();
        USB_BOARD_LINK.shutdown();
        CH585_ROLE_BOOTSTRAP.shutdown();
        enterWebSafeState();
        LOG_ERROR("WEBCONFIG",
                  "Secure WebHID identity/session gate failed");
        return;
    }
    BOARD_POWER.releaseSafeState();

    // QSPI remains memory mapped for configuration/assets/OTA, not web pages.
    int8_t qspi_result = QSPI_W25Qxx_EnterMemoryMappedMode();
    if (qspi_result != 0) {
        LOG_ERROR("WEBCONFIG", "Failed to enter QSPI memory mapped mode, error: %d", qspi_result);
    }

    // 切换到校准/连续采样模式
    ADCManager::getInstance().setADCMode(ADC_MODE_CONTINUOUS);
    BOARD_POWER.setHallEnabled(true);
    HAL_Delay(BOARD_HALL_STABILIZE_MS);
    ADCManager::getInstance().startContinuousSampling();

    // 初始化LED管理器
    LEDS_MANAGER.setup();
    // WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_KEYS, 0);
    // WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_AMBIENT, 0);

    isRunning = true;

    // Logger_Flush();
}

void WebConfigState::loop() {
    if(isRunning) {
        if (BOARD_MODE.consumeChanged() &&
            BOARD_MODE.current() != BoardMode::Usb) {
            reset();
            enterWebSafeState();
            return;
        }
        USB_DRIVER.process();
        ADC_CALIBRATION_MANAGER.processCalibration(); // 处理校准逻辑
        
        // 实时更新按键状态并生成事件（在主循环中调用）
        WEBCONFIG_BTNS_MANAGER.update();
        WEBHID_SERVICE.process();
        // 更新LED预览效果
        WEBCONFIG_LEDS_MANAGER.update(WEBCONFIG_BTNS_MANAGER.getCurrentMask());
    }
}

void WebConfigState::reset() {
    isRunning = false;
    /*
     * Remove the WebHID/ConfigTransport callbacks and destroy all session
     * material before disconnecting or powering down the CH585 link.
     */
    WEBHID_SERVICE.shutdown();
    (void)ADC_CALIBRATION_MANAGER.stopCalibration();
    ADCManager::getInstance().forceStopAllSampling();
#if HAS_LED == 1
    LEDS_MANAGER.deinit();
#endif
    BOARD_POWER.setHallEnabled(false);
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
    
    // 清除LED预览模式，恢复默认配置
    WEBCONFIG_LEDS_MANAGER.clearPreviewConfig();
    
}
