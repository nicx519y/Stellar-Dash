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
    ADC_BTNS_WORKER.setup();
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
    ADC_BTNS_WORKER.setup();
    GPIO_BTNS_WORKER.setup();
    GAMEPAD.setup();
#if HAS_LED == 1
    LEDS_MANAGER.setup();
#endif

    const uint16_t reportRateHz = static_cast<uint16_t>(
        STORAGE_MANAGER.getWirelessReportRate());
    REPORT_SCHEDULER.start(reportRateHz);
    inputPipelineRunning = true;
    APP_STAGE("I04", "input pipeline ready: report rate=%u Hz",
              static_cast<unsigned>(reportRateHz));
}

void InputState::stopInputPipeline()
{
    if (!inputPipelineRunning) {
        REPORT_SCHEDULER.stop();
        return;
    }

    REPORT_SCHEDULER.stop();
#if HAS_LED == 1
    LEDS_MANAGER.deinit();
#endif
    (void)ADC_BTNS_WORKER.deinit();
    GAMEPAD.deinit();
    virtualPinMask = 0u;
    lastVirtualPinMask = 0u;
    inputPipelineRunning = false;
}

void InputState::processReportTick()
{
    virtualPinMask = GPIO_BTNS_WORKER.read() | ADC_BTNS_WORKER.read();
    SystemSleep_NotifyButtonActivity(HAL_GetTick(), virtualPinMask);

    if ((virtualPinMask & FN_BUTTON_VIRTUAL_PIN) == 0u) {
        GAMEPAD.read(virtualPinMask);
        const uint32_t reportSequence = MonitorTelemetry_NextSequence();
        MonitorTelemetry_OnReportReady(reportSequence);

        if (activeBoardMode == BoardMode::Usb) {
#if APPLICATION_DEBUG_PRINT == 1
            LATENCY_MONITOR.processingCompleted();
#endif
            MonitorTelemetry_SetPendingUsbSeq(reportSequence);
            if (USB_DRIVER.submit(GAMEPAD.state)) {
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
#if APPLICATION_DEBUG_PRINT == 1
        LATENCY_MONITOR.processingCompleted();
#endif
    }
    lastVirtualPinMask = virtualPinMask;
}

bool InputState::applyPhysicalMode(BoardMode mode, bool initial)
{
    const InputMode inputMode = STORAGE_MANAGER.getInputMode();
    const WirelessReportRate wirelessRate =
        STORAGE_MANAGER.getWirelessReportRate();

    APP_STAGE("I02", "apply physical mode: mode=%u initial=%u input=%u rate=%u",
              static_cast<unsigned>(mode), initial ? 1u : 0u,
              static_cast<unsigned>(inputMode),
              static_cast<unsigned>(wirelessRate));

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
        CONNECTION_MANAGER.setup(CONNECTION_MODE_USB, wirelessRate);
        BOARD_POWER.releaseSafeState();
        APP_STAGE("I03", "CH585 USB role locked; safe power state released");
        startInputPipeline();
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
        CONNECTION_MANAGER.setup(CONNECTION_MODE_RF24G, wirelessRate);
        BOARD_POWER.releaseSafeState();
        APP_STAGE("I03", "CH585 RF role locked; safe power state released");
        startInputPipeline();
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

    /*
     * The configured rate controls ADC sampling in both physical roles.
     * RF mode additionally applies the same rate to the TX-to-RX packet path.
     */
    if (inputPipelineRunning) {
        const uint16_t desiredReportRateHz = static_cast<uint16_t>(
            STORAGE_MANAGER.getWirelessReportRate());
        if (REPORT_SCHEDULER.getRate() != desiredReportRateHz) {
            REPORT_SCHEDULER.setRate(desiredReportRateHz);
        }
    }

#if RF24G_SPI_BRINGUP_TX_ONLY
    if (activeBoardMode == BoardMode::Rf) {
        uint8_t sent = 0u;
        while ((sent < RF24G_SPI_BRINGUP_TX_CATCHUP_LIMIT) &&
               REPORT_SCHEDULER.consumeTick()) {
            const uint32_t reportSequence =
                MonitorTelemetry_NextSequence();
            MonitorTelemetry_OnReportReady(reportSequence);
            CONNECTION_MANAGER.onReportReady(GAMEPAD.state,
                                             reportSequence);
            ++sent;
        }
        return;
    }
#endif

    if (inputPipelineRunning && activeBoardMode == BoardMode::Rf) {
        if (REPORT_SCHEDULER.consumeLatestTick()) {
            processReportTick();
        }
    } else if (inputPipelineRunning && activeBoardMode == BoardMode::Usb) {
        while (REPORT_SCHEDULER.consumeTick()) {
            processReportTick();
        }
    }

    if (inputPipelineRunning && activeBoardMode == BoardMode::Usb) {
        USB_DRIVER.process();
    }

#if HAS_LED == 1
    if (inputPipelineRunning) {
        LEDS_MANAGER.loop(virtualPinMask);
    }
#endif
#if APPLICATION_DEBUG_PRINT == 1
    LATENCY_MONITOR.process();
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
