#include "states/calibration_state.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "pwm-ws2812b.h"
#include "storagemanager.hpp"
#include "adc_btns/adc_manager.hpp"
#include "board_mode.hpp"
#include "board_power.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "main_runtime_control.hpp"

#include "system_logger.h"

namespace {

static bool calibrationModeIsValid()
{
    return BOARD_MODE.isStable() &&
           (BOARD_MODE.current() == BoardMode::Usb ||
            BOARD_MODE.current() == BoardMode::Rf);
}

static void enterCalibrationSafeState()
{
    SPIScreenManager::getInstance().shutdown();
    BOARD_POWER.enterSafeState();
}

} // namespace

// 定义静态成员变量
uint32_t CalibrationState::rebootTime = 0;

void CalibrationState::allCalibrationCompletedCallback(uint8_t totalButtons, uint8_t successCount, uint8_t failedCount) {
    LOG_INFO("CALIBRATION", "Calibration completed - total: %d, success: %d, failed: %d", 
             totalButtons, successCount, failedCount);
    
    ADC_CALIBRATION_MANAGER.stopCalibration();
    ADC_MANAGER.forceStopAllSampling();
#if HAS_LED == 1
    (void)WS2812B_StopStrip(WS2812B_STRIP_KEYS);
#endif
    BOARD_POWER.setHallEnabled(false);
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();
    
    LOG_INFO("CALIBRATION", "Boot mode changed to INPUT, system will reboot in 1 second");
    rebootTime = HAL_GetTick();
}

bool CalibrationState::enter() {
    LOG_INFO("CALIBRATION", "Starting calibration state setup");
    APP_DBG("CalibrationState::setup");

    if (!BOARD_MODE.isStable()) {
        BOARD_MODE.update(HAL_GetTick());
    }
    (void)BOARD_MODE.consumeChanged();
    if (!calibrationModeIsValid()) {
        exit();
        enterCalibrationSafeState();
        LOG_ERROR("CALIBRATION", "Physical switch is center/fault; calibration inhibited");
        return false;
    }

    BOARD_POWER.releaseSafeState();
    BOARD_POWER.setHallEnabled(true);
    HAL_Delay(BOARD_HALL_STABILIZE_MS);
    
    // 切换到校准/连续采样模式
    ADCManager::getInstance().setADCMode(ADC_MODE_CONTINUOUS);
    // 启动连续采样
    ADCManager::getInstance().startContinuousSampling();

    // 启动手动校准模式
    ADC_CALIBRATION_MANAGER.setAllCalibrationCompletedCallback(allCalibrationCompletedCallback);
    const ADCBtnsError startResult =
        ADC_CALIBRATION_MANAGER.startManualCalibration();
    if (startResult != ADCBtnsError::SUCCESS) {
        exit();
        enterCalibrationSafeState();
        LOG_ERROR("CALIBRATION", "Failed to start calibration: %d",
                  static_cast<int>(startResult));
        return false;
    }
    
    isRunning = true;
    rebootTime = 0;
    
    LOG_INFO("CALIBRATION", "Calibration state setup completed - waiting for user input");
    Logger_Flush();
    return true;
}

void CalibrationState::tick() {
    if (BOARD_MODE.consumeChanged()) {
        if (!calibrationModeIsValid()) {
            exit();
            enterCalibrationSafeState();
            return;
        }
        if (!isRunning) {
            (void)enter();
            return;
        }
    }

    if (!isRunning && calibrationModeIsValid()) {
        (void)enter();
        return;
    }

    if (isRunning) {
        // 如果校准完成，等待1秒后重启
        if(rebootTime > 0 && HAL_GetTick() - rebootTime >= 1000) {
            LOG_INFO("CALIBRATION", "Initiating system reboot after calibration completion");
            Logger_Flush(); // 确保日志被写入Flash
            MainRuntime_RequestReset();
        } else {    
            ADC_CALIBRATION_MANAGER.processCalibration();
        }
        // 可根据需要添加更多校准相关的处理逻辑
    }
}

void CalibrationState::exit() {
    (void)ADC_CALIBRATION_MANAGER.stopCalibration();
    ADC_CALIBRATION_MANAGER.clearCallbacks();
    ADC_MANAGER.forceStopAllSampling();
#if HAS_LED == 1
    (void)WS2812B_StopStrip(WS2812B_STRIP_KEYS);
#endif
    BOARD_POWER.setHallEnabled(false);
    isRunning = false;
    rebootTime = 0;
    
    // 重置状态标志
    static bool calibrationStatusLogged = false;
    calibrationStatusLogged = false;
    
    LOG_DEBUG("CALIBRATION", "Calibration state reset completed");
    // 可根据需要添加退出校准模式的处理逻辑
} 


