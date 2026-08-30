#include "input_state.hpp"

#include "adc_btns/adc_btns_worker.hpp"
#include "board_cfg.h"
#include "board_power.hpp"
#include "ch585_role_bootstrap.hpp"
#include "connection_manager.hpp"
#include "gamepad.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "hotkeys_manager.hpp"
#include "latency_monitor.hpp"
#include "leds/leds_manager.hpp"
#include "monitor_telemetry.hpp"
#include "report_scheduler.hpp"
#include "rf_bridge_port.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "storagemanager.hpp"
#include "system_logger.h"
#include "system_sleep_manager.hpp"
#include "usb_board_link.hpp"
#include "usbdriver.hpp"

namespace {

#ifndef RF24G_SPI_BRINGUP_TX_ONLY
#define RF24G_SPI_BRINGUP_TX_ONLY 0
#endif

#ifndef RF24G_SPI_BRINGUP_TX_CATCHUP_LIMIT
#define RF24G_SPI_BRINGUP_TX_CATCHUP_LIMIT 2u
#endif

static void onDefaultProfileChanged()
{
    if (BOARD_POWER.isSafeLatched() ||
        !INPUT_STATE.isInputPipelineRunning() ||
        !CH585_ROLE_BOOTSTRAP.isLocked()) {
        return;
    }
#if RF24G_SPI_BRINGUP_TX_ONLY
    if (CONNECTION_MANAGER.getMode() == CONNECTION_MODE_RF24G) {
        return;
    }
#endif
    const ADCBtnsError result = ADC_BTNS_WORKER.setup();
    if (result != ADCBtnsError::SUCCESS) {
        APP_ERR("Default profile ADC worker setup failed: %d",
                static_cast<int>(result));
    }
    GPIO_BTNS_WORKER.setup();
}

static void enterBoardSafeState()
{
    /* CH585/USB/RF failures must not take down the independent local UI. */
    BOARD_POWER.enterSafeState();
}

static void teardownCh585Runtime()
{
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
}

} // namespace

void InputState::startInputPipeline()
{
    if (inputPipelineRunning) {
        return;
    }

    APP_STAGE("I04", "input pipeline initialization begin");

    /*
     * Center/fault mode powers Hall off.  Re-enable it and honor the board
     * settling interval before calibration or DMA sampling resumes.
     */
    BOARD_POWER.setHallEnabled(true);
    HAL_Delay(BOARD_HALL_STABILIZE_MS);
    const ADCBtnsError adcResult = ADC_BTNS_WORKER.setup();
    if (adcResult != ADCBtnsError::SUCCESS) {
        ADC_MANAGER.forceStopAllSampling();
        BOARD_POWER.setHallEnabled(false);
        APP_STAGE_ERROR("I04", "ADC circular DMA setup failed: %d",
                        static_cast<int>(adcResult));
        return;
    }
    GPIO_BTNS_WORKER.setup();
    GAMEPAD.setup();
#if HAS_LED == 1
#if INPUT_LED_RECOVERY_HOLD_OFF
    /*
     * Do not call LEDsManager::setup(): that is the point which powers the 5V
     * LED rail and starts both circular TIM4 DMA streams.  Board initialization
     * already left the two rails off; assert that state again for recovery.
     */
    BOARD_POWER.setKeyLedEnabled(false);
    BOARD_POWER.setAmbientLedEnabled(false);
    APP_STAGE("I06", "LED recovery hold active: key/ambient rails and TIM4 DMA remain off");
#else
    LEDS_MANAGER.setup();
#endif
#endif

    const InputMode inputMode = STORAGE_MANAGER.getInputMode();
    const uint16_t requestedReportRateHz = static_cast<uint16_t>(
        STORAGE_MANAGER.getWirelessReportRate());
    const uint16_t reportRateHz = activeBoardMode == BoardMode::Usb
        ? USB_DRIVER.effectiveReportRateHz(inputMode,
                                           requestedReportRateHz)
        : CONNECTION_MANAGER.getAppliedReportRateHz();
    if (activeBoardMode == BoardMode::Rf &&
        !CONNECTION_MANAGER.isReportRateConfirmed()) {
#if HAS_LED == 1
        LEDS_MANAGER.deinit();
#endif
        GAMEPAD.deinit();
        ADC_MANAGER.forceStopAllSampling();
        BOARD_POWER.setHallEnabled(false);
        APP_STAGE_ERROR("I04", "RF report rate was not confirmed");
        return;
    }
    if (!REPORT_SCHEDULER.start(reportRateHz)) {
#if HAS_LED == 1
        LEDS_MANAGER.deinit();
#endif
        GAMEPAD.deinit();
        ADC_MANAGER.forceStopAllSampling();
        BOARD_POWER.setHallEnabled(false);
        APP_STAGE_ERROR("I04", "TIM2 report/ADC sampling clock start failed");
        return;
    }
    MonitorTelemetry_SetTargetRateHz(reportRateHz);
    inputPipelineRunning = true;
    APP_STAGE("I04", "input pipeline ready: report rate=%u Hz",
              static_cast<unsigned>(reportRateHz));
}

void InputState::stopInputPipeline()
{
    if (!inputPipelineRunning) {
        REPORT_SCHEDULER.stop();
        ADC_MANAGER.forceStopAllSampling();
        return;
    }

    REPORT_SCHEDULER.stop();
    ADC_MANAGER.forceStopAllSampling();
#if HAS_LED == 1
    LEDS_MANAGER.deinit();
#endif
    (void)ADC_BTNS_WORKER.deinit();
    GAMEPAD.deinit();
    virtualPinMask = 0u;
    lastVirtualPinMask = 0u;
    inputPipelineRunning = false;
}

bool InputState::suspendInputPipelineForStorage()
{
    const bool wasRunning = inputPipelineRunning;
    if (wasRunning) {
        APP_STAGE("I08", "input pipeline suspended for QSPI storage transaction");
        stopInputPipeline();
    }
    return wasRunning;
}

bool InputState::resumeInputPipelineAfterStorage(bool wasRunning)
{
    if (!wasRunning) {
        return true;
    }
    if (!isRunning || activeBoardMode == BoardMode::CenterOff ||
        activeBoardMode == BoardMode::Fault) {
        return false;
    }
    startInputPipeline();
    const bool resumed = inputPipelineRunning;
    if (resumed) {
        APP_STAGE("I08", "input pipeline resumed after QSPI storage transaction");
    } else {
        APP_STAGE_ERROR("I08", "input pipeline failed to resume after QSPI storage transaction");
    }
    return resumed;
}

void InputState::processReportSample(const AdcSampleFrame& sample)
{
    virtualPinMask = GPIO_BTNS_WORKER.read() | ADC_BTNS_WORKER.read(sample);
    SystemSleep_NotifyButtonActivity(HAL_GetTick(), virtualPinMask);

    if ((virtualPinMask & FN_BUTTON_VIRTUAL_PIN) == 0u) {
        GAMEPAD.read(virtualPinMask);
        const uint32_t reportSequence = MonitorTelemetry_NextSequence();
        MonitorTelemetry_OnReportReady(reportSequence,
                                       sample.triggerCycles,
                                       sample.completeCycles);

        if (activeBoardMode == BoardMode::Usb) {
            const uint32_t age =
                MICROS_TIMER.elapsedMicros(sample.triggerCycles);
            const uint16_t ageUs = static_cast<uint16_t>(
                age > UINT16_MAX ? UINT16_MAX : age);
            MonitorTelemetry_SetPendingUsbSeq(reportSequence);
            if (USB_DRIVER.submit(GAMEPAD.state, ageUs)) {
                MonitorTelemetry_OnUsbReportSubmitted(
                    USB_BOARD_INPUT_V1_BYTES);
            } else {
                MonitorTelemetry_OnError(
                    "USB_BOARD_LINK",
                    2001u,
                    "CH585 input submission failed");
            }
        } else if (activeBoardMode == BoardMode::Rf) {
            /*
             * Frozen path: ConnectionManager -> RFTransport -> 0xA5 remains
             * byte-for-byte unchanged.
             */
            CONNECTION_MANAGER.onReportReady(GAMEPAD.state, reportSequence);
        }
    } else {
        HOTKEYS_MANAGER.updateHotkeyState(virtualPinMask,
                                          lastVirtualPinMask);
    }
    lastVirtualPinMask = virtualPinMask;
}

bool InputState::applyPhysicalMode(BoardMode mode,
                                   bool initial,
                                   bool compatibilityRecovery)
{
    const InputMode inputMode = STORAGE_MANAGER.getInputMode();
    const WirelessReportRate wirelessRate =
        STORAGE_MANAGER.getWirelessReportRate();

    APP_STAGE("I02", "apply physical mode: mode=%u initial=%u input=%u rate=%u",
              static_cast<unsigned>(mode), initial ? 1u : 0u,
              static_cast<unsigned>(inputMode),
              static_cast<unsigned>(wirelessRate));

    if (!compatibilityRecovery) {
        usbCompatibilityRecoveryUsed = false;
    }

    stopInputPipeline();
    RFBridgePort_Shutdown();
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    activeBoardMode = mode;
    usbRuntimeInitialized = false;
    usbRuntimeConnected = false;

    CH585_ROLE_BOOTSTRAP.setSelector(UsbBoardLink_SelectRoleCallback);

    if (mode == BoardMode::Usb) {
        USB_DRIVER.setRequestedReportRateHz(
            static_cast<uint16_t>(wirelessRate));
        USB_DRIVER.setFastInputAllowed(!compatibilityRecovery);
        if (!CH585_ROLE_BOOTSTRAP.start(Ch585Role::Usb) ||
            !USB_DRIVER.start(inputMode)) {
            APP_STAGE_ERROR("I03", "CH585 USB role or USB runtime startup failed");
            teardownCh585Runtime();
            enterBoardSafeState();
            activeBoardMode = BoardMode::Fault;
            return false;
        }
        usbRuntimeInitialized = true;
        usbRuntimeConnected = true;
        CONNECTION_MANAGER.setup(CONNECTION_MODE_USB,
                                 wirelessRate,
                                 inputMode);
        BOARD_POWER.releaseSafeState();
        APP_STAGE("I03", "CH585 USB role locked; safe power state released");
        startInputPipeline();
        if (!inputPipelineRunning) {
            teardownCh585Runtime();
            enterBoardSafeState();
            activeBoardMode = BoardMode::Fault;
            return false;
        }
        return true;
    }

    if (mode == BoardMode::Rf) {
        if (!CH585_ROLE_BOOTSTRAP.start(Ch585Role::Rf)) {
            APP_STAGE_ERROR("I03", "CH585 RF role startup failed");
            teardownCh585Runtime();
            enterBoardSafeState();
            activeBoardMode = BoardMode::Fault;
            return false;
        }
        CONNECTION_MANAGER.setup(CONNECTION_MODE_RF24G,
                                 wirelessRate,
                                 inputMode);
        if (!CONNECTION_MANAGER.isReportRateConfirmed()) {
            APP_STAGE_ERROR("I03",
                            "CH585 RF role started but no report rate was confirmed");
            teardownCh585Runtime();
            enterBoardSafeState();
            activeBoardMode = BoardMode::Fault;
            return false;
        }
        BOARD_POWER.releaseSafeState();
        APP_STAGE("I03", "CH585 RF role locked; safe power state released");
        startInputPipeline();
        if (!inputPipelineRunning) {
            teardownCh585Runtime();
            enterBoardSafeState();
            activeBoardMode = BoardMode::Fault;
            return false;
        }
        return true;
    }

    /*
     * Center/off and the impossible 0/0 combination are fail-safe states:
     * CH585, host VBUS and optional high-current rails remain off.
     */
    enterBoardSafeState();
    APP_STAGE_ERROR("I03", "physical mode is center/fault; input transport remains safe");
    return false;
}

bool InputState::enter()
{
    const InputMode inputMode = STORAGE_MANAGER.getInputMode();
    APP_STAGE("I01", "INPUT state setup begin: input mode=%u",
              static_cast<unsigned>(inputMode));
    if (inputMode == INPUT_MODE_CONFIG) {
        APP_STAGE_ERROR("I01", "CONFIG profile is invalid in INPUT state");
        APP_ERR("INPUT mode cannot use CONFIG profile");
        activeBoardMode = BoardMode::Fault;
        enterBoardSafeState();
        return false;
    }

    if (!BOARD_MODE.isStable()) {
        BOARD_MODE.update(HAL_GetTick());
    }
    (void)BOARD_MODE.consumeChanged();
    const bool modeReady = applyPhysicalMode(BOARD_MODE.current(), true);

    STORAGE_MANAGER.registerDefaultProfileChangedCallback(
        onDefaultProfileChanged);
    isRunning = true;
    APP_STAGE("I05", "INPUT state loop enabled: transport ready=%u",
              modeReady ? 1u : 0u);
    Logger_Flush();
    return true;
}

void InputState::tick()
{
    if (!isRunning) {
        return;
    }

    if (BOARD_MODE.consumeChanged()) {
        (void)applyPhysicalMode(BOARD_MODE.current(), false);
    }

    if (inputPipelineRunning && activeBoardMode == BoardMode::Usb) {
        USB_DRIVER.process();
        if (USB_DRIVER.takeCompatibilityRecoveryRequest()) {
            if (!usbCompatibilityRecoveryUsed) {
                usbCompatibilityRecoveryUsed = true;
                APP_STAGE_ERROR(
                    "I09",
                    "BoardLink control plane recovery: reinitializing CH585 USB role once at 1 kHz");
                (void)applyPhysicalMode(BoardMode::Usb, false, true);
            } else {
                APP_STAGE_ERROR(
                    "I09",
                    "BoardLink control plane recovery already used; entering fault state");
                stopInputPipeline();
                teardownCh585Runtime();
                enterBoardSafeState();
                activeBoardMode = BoardMode::Fault;
            }
            return;
        }
    }

    /*
     * USB uses the post-probe effective rate. RF uses only the rate confirmed
     * by RATE_APPLIED (including an explicit 1-kHz fallback).
     */
    if (inputPipelineRunning) {
        if (!ADC_MANAGER.isDmaSamplingActive() ||
            !ADC_MANAGER.isInputSampleStreamHealthy()) {
            APP_STAGE_ERROR("I07", "ADC circular DMA stopped unexpectedly");
            stopInputPipeline();
            teardownCh585Runtime();
            enterBoardSafeState();
            activeBoardMode = BoardMode::Fault;
            return;
        }
        const uint16_t requestedReportRateHz = static_cast<uint16_t>(
            STORAGE_MANAGER.getWirelessReportRate());
        const uint16_t desiredReportRateHz = activeBoardMode == BoardMode::Usb
            ? USB_DRIVER.effectiveReportRateHz(
                  STORAGE_MANAGER.getInputMode(), requestedReportRateHz)
            : CONNECTION_MANAGER.getAppliedReportRateHz();
        if (REPORT_SCHEDULER.getRate() != desiredReportRateHz) {
            if (!REPORT_SCHEDULER.setRate(desiredReportRateHz)) {
                APP_STAGE_ERROR("I07", "TIM2 report/ADC rate change failed");
                stopInputPipeline();
                teardownCh585Runtime();
                enterBoardSafeState();
                activeBoardMode = BoardMode::Fault;
                return;
            }
            MonitorTelemetry_SetTargetRateHz(desiredReportRateHz);
        }
    }

#if RF24G_SPI_BRINGUP_TX_ONLY
    if (activeBoardMode == BoardMode::Rf) {
        AdcSampleFrame sample = {};
        if (ADC_MANAGER.consumeLatestInputSample(sample)) {
            const uint32_t reportSequence =
                MonitorTelemetry_NextSequence();
            MonitorTelemetry_OnReportReady(reportSequence,
                                           sample.triggerCycles,
                                           sample.completeCycles);
            CONNECTION_MANAGER.onReportReady(GAMEPAD.state,
                                             reportSequence);
        }
        return;
    }
#endif

    if (inputPipelineRunning &&
        (activeBoardMode == BoardMode::Rf ||
         activeBoardMode == BoardMode::Usb)) {
        AdcSampleFrame sample = {};
        if (ADC_MANAGER.consumeLatestInputSample(sample)) {
            processReportSample(sample);
        }
    }

#if APPLICATION_DEBUG_PRINT == 1
    LATENCY_MONITOR.process();
#endif
}

void InputState::serviceLeds()
{
#if HAS_LED == 1
#if !INPUT_LED_RECOVERY_HOLD_OFF
    if (isRunning && inputPipelineRunning) {
        LEDS_MANAGER.loop(virtualPinMask);
    }
#endif
#endif
}

bool InputState::ensureUsbRuntime(InputMode inputMode)
{
    if (activeBoardMode != BoardMode::Usb) {
        return false;
    }
    if (usbRuntimeInitialized && usbRuntimeConnected) {
        return true;
    }
    USB_DRIVER.setRequestedReportRateHz(static_cast<uint16_t>(
        STORAGE_MANAGER.getWirelessReportRate()));
    usbRuntimeInitialized = USB_DRIVER.start(inputMode);
    usbRuntimeConnected = usbRuntimeInitialized;
    return usbRuntimeInitialized;
}

void InputState::sendUsbNeutralReport()
{
    if (!usbRuntimeInitialized) {
        return;
    }
    for (uint8_t index = 0u; index < 4u; ++index) {
        (void)USB_DRIVER.sendNeutral();
        USB_DRIVER.process();
        HAL_Delay(1u);
    }
}

bool InputState::disconnectUsbRuntime()
{
    if (!usbRuntimeInitialized) {
        return true;
    }
    sendUsbNeutralReport();
    USB_DRIVER.shutdown();
    usbRuntimeConnected = false;
    usbCompatibilityRecoveryUsed = false;
    usbRuntimeInitialized = false;
    return true;
}

bool InputState::connectUsbRuntime()
{
    return ensureUsbRuntime(STORAGE_MANAGER.getInputMode());
}

void InputState::exit()
{
    stopInputPipeline();
    USB_DRIVER.shutdown();
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    RFBridgePort_Shutdown();
    lastVirtualPinMask = 0u;
    virtualPinMask = 0u;
    usbRuntimeInitialized = false;
    usbRuntimeConnected = false;
    activeBoardMode = BoardMode::CenterOff;
    isRunning = false;
}
