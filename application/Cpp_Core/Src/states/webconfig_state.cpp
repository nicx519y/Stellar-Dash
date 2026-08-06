#include "states/webconfig_state.hpp"
#include "board_cfg.h"
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

static void enterWebFailureUiState()
{
    /*
     * Keep the transport and all optional/high-current rails fail-closed. The
     * dedicated recovery permission lets only the LCD/rotary owner present an
     * actionable page while the global safe latch remains asserted.
     */
    SPIScreenManager::getInstance().shutdown();
    BOARD_POWER.enterRecoveryUiState();
    /* setup() is idempotent (`g_inited` guarded). Recreate and paint the
     * recovery UI here so both initial-gate and runtime-switch failures are
     * visible without relying on a later owner transition. */
    SPIScreenManager::getInstance().setup();
    SPIScreenManager::getInstance().loop();
}

} // namespace

void WebConfigState::setup() {

    LOG_INFO("WEBCONFIG", "Starting web configuration state setup");
    APP_DBG("WebConfigState::setup");
    APP_STAGE("W01", "WebConfig state setup begin");
    retryRequested = false;
    recoveryUiPending = false;
    isRunning = false;
    runtimeStatus = WebConfigRuntimeStatus::Starting;

    /*
     * WebConfig is permitted only in the physical USB switch position and
     * always uses the independent CH585 maintenance role.
     */
    if (!BOARD_MODE.isStable() || BOARD_MODE.current() != BoardMode::Usb) {
        APP_STAGE_ERROR("W02", "physical USB mode gate failed: mode=%u stable=%u",
                        static_cast<unsigned>(BOARD_MODE.current()),
                        BOARD_MODE.isStable() ? 1u : 0u);
        enterFailure(WebConfigRuntimeStatus::ErrorUsbMode);
        LOG_ERROR("WEBCONFIG", "Physical switch is not in USB position");
        return;
    }
    APP_STAGE("W02", "physical USB mode gate accepted");

    /* Paint the starting state before the bounded CH585 capability handshake. */
    BOARD_POWER.releaseSafeState();
    APP_STAGE("W03", "LCD recovery UI power released for WebConfig startup");
    SPIScreenManager::getInstance().setup();
    SPIScreenManager::getInstance().loop();

    WEBHID_SERVICE.shutdown();
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    CH585_ROLE_BOOTSTRAP.setSelector(UsbBoardLink_SelectRoleCallback);
    if (!CH585_ROLE_BOOTSTRAP.start(Ch585Role::Maintenance) ||
        !USB_DRIVER.start(InputMode::INPUT_MODE_CONFIG)) {
        APP_STAGE_ERROR("W04", "CH585 maintenance role or CONFIG USB startup failed");
        enterFailure(WebConfigRuntimeStatus::ErrorMaintenance);
        LOG_ERROR("WEBCONFIG", "CH585 maintenance capability gate failed");
        return;
    }
    APP_STAGE("W04", "CH585 maintenance role and CONFIG USB runtime ready");
    /*
     * V2 WebConfig is hosted by the HTTPS server.  Do not start the legacy
     * RNDIS/NCM, LwIP, httpd or WebSocket stack here.  The command handlers
     * remain the single source of configuration semantics and are invoked by
     * the authenticated WebHID dispatcher.
     */
    WebSocketCommandManager::getInstance().initializeHandlers();
    APP_STAGE("W05", "WebConfig command handlers initialized");
    if (!WEBHID_SERVICE.setup()) {
        APP_STAGE_ERROR("W06", "secure WebHID service setup failed");
        enterFailure(WebConfigRuntimeStatus::ErrorSecurity);
        LOG_ERROR("WEBCONFIG",
                  "Secure WebHID identity/session gate failed");
        return;
    }
    APP_STAGE("W06", "secure WebHID service ready; awaiting browser authentication");
    BOARD_POWER.releaseSafeState();

    // QSPI remains memory mapped for configuration/assets/OTA, not web pages.
    int8_t qspi_result = QSPI_W25Qxx_EnterMemoryMappedMode();
    if (qspi_result != 0) {
        APP_STAGE_ERROR("W07", "QSPI memory-mapped mode failed: %d", qspi_result);
        LOG_ERROR("WEBCONFIG", "Failed to enter QSPI memory mapped mode, error: %d", qspi_result);
        enterFailure(WebConfigRuntimeStatus::ErrorStorageInit);
        return;
    }
    APP_STAGE("W07", "QSPI memory-mapped mode ready for config/assets/OTA");

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
    runtimeStatus = WebConfigRuntimeStatus::Ready;
    APP_STAGE("W08", "WebConfig runtime ready");

    // Logger_Flush();
}

void WebConfigState::loop() {
    if (recoveryUiPending) {
        recoveryUiPending = false;
        enterWebFailureUiState();
        return;
    }

    if (retryRequested) {
        retryRequested = false;
        setup();
        return;
    }

    if(isRunning) {
        if (BOARD_MODE.consumeChanged() &&
            BOARD_MODE.current() != BoardMode::Usb) {
            enterFailure(WebConfigRuntimeStatus::ErrorUsbMode);
            return;
        }
        USB_DRIVER.process();
        ADC_CALIBRATION_MANAGER.processCalibration(); // 处理校准逻辑
        
        // 实时更新按键状态并生成事件（在主循环中调用）
        WEBCONFIG_BTNS_MANAGER.update();
        WEBHID_SERVICE.process();
        const bool authenticated = WEBHID_SERVICE.isAuthenticated();
        if (authenticated &&
            runtimeStatus != WebConfigRuntimeStatus::Authenticated) {
            APP_STAGE("W09", "browser WebHID session authenticated");
        }
        runtimeStatus = authenticated
            ? WebConfigRuntimeStatus::Authenticated
            : WebConfigRuntimeStatus::Ready;
        // 更新LED预览效果
        WEBCONFIG_LEDS_MANAGER.update(WEBCONFIG_BTNS_MANAGER.getCurrentMask());
    }
}

void WebConfigState::reset() {
    isRunning = false;
    retryRequested = false;
    recoveryUiPending = false;
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

bool WebConfigState::canRetry() const
{
    return runtimeStatus == WebConfigRuntimeStatus::ErrorUsbMode ||
           runtimeStatus == WebConfigRuntimeStatus::ErrorMaintenance ||
           runtimeStatus == WebConfigRuntimeStatus::ErrorSecurity ||
           runtimeStatus == WebConfigRuntimeStatus::ErrorStorageInit;
}

void WebConfigState::requestRetry()
{
    if (canRetry()) {
        retryRequested = true;
    }
}

void WebConfigState::reportStorageFailure()
{
    /* Called from the screen input callback: defer LCD power-state changes
     * until the next state-machine pass, after the active frame completes. */
    reset();
    runtimeStatus = WebConfigRuntimeStatus::ErrorStorage;
    recoveryUiPending = true;
    LOG_ERROR("WEBCONFIG", "Failed to persist WebConfig exit mode");
}

void WebConfigState::enterFailure(WebConfigRuntimeStatus failureStatus)
{
    APP_STAGE_ERROR("W99", "WebConfig entered recovery UI: status=%u",
                    static_cast<unsigned>(failureStatus));
    reset();
    runtimeStatus = failureStatus;
    enterWebFailureUiState();
}
