#include "main_state_machine.hpp"
#include "main_runtime_control.hpp"

#include "adc_btns/adc_manager.hpp"
#include "board_cfg.h"
#include "board_mode.hpp"
#include "board_power.hpp"
#include "ch585_firmware_update.hpp"
#include "ch585_update_mode.hpp"
#include "connection_manager.hpp"
#include "power_manager.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "states/calibration_state.hpp"
#include "states/ch585_bridge_update_state.hpp"
#include "states/ch585_usb_isp_state.hpp"
#include "states/safe_recovery_state.hpp"
#include "system_logger.h"

namespace {

static_assert(static_cast<unsigned>(MainRuntimeState::SafeRecovery) + 1u == 6u,
              "The STM32 top-level runtime must contain exactly six states");

static bool isValidBootMode(BootMode mode)
{
    return mode == BootMode::BOOT_MODE_INPUT ||
           mode == BootMode::BOOT_MODE_WEB_CONFIG ||
           mode == BootMode::BOOT_MODE_CALIBRATION;
}

} // namespace

BaseState* MainStateMachine::stateFor(MainRuntimeState selected) const
{
    switch (selected) {
        case MainRuntimeState::Input: return &INPUT_STATE;
        case MainRuntimeState::WebConfig: return &WEB_CONFIG_STATE;
        case MainRuntimeState::Calibration: return &CALIBRATION_STATE;
        case MainRuntimeState::Ch585UsbIsp: return &CH585_USB_ISP_STATE;
        case MainRuntimeState::Ch585BridgeUpdate:
            return &CH585_BRIDGE_UPDATE_STATE;
        case MainRuntimeState::SafeRecovery: return &SAFE_RECOVERY_STATE;
    }
    return &SAFE_RECOVERY_STATE;
}

void MainStateMachine::initializeInteractiveRuntime()
{
    if (interactiveRuntimeInitialized) return;

    const LogResult logResult = Logger_Init(false, LOG_LEVEL_DEBUG);
    APP_STAGE(logResult == LOG_RESULT_SUCCESS ? "A08" : "A08E",
              "persistent logger initialization result=%d", logResult);

    BOARD_MODE.setup();
    APP_STAGE("A11", "physical mode sampled: mode=%u stable=%u",
              static_cast<unsigned>(BOARD_MODE.current()),
              BOARD_MODE.isStable() ? 1u : 0u);
    STORAGE_MANAGER.initConfig();
    APP_STAGE("A12", "configuration loaded: boot mode=%u",
              static_cast<unsigned>(STORAGE_MANAGER.getBootMode()));

    /* Every interactive state gets the independent recovery display and
     * power telemetry. BridgeUpdate is deliberately dispatched before this
     * point so writable QSPI can never overlap screen asset reads. */
    BOARD_POWER.enterRecoveryUiState();
    SPIScreenManager::getInstance().setup();
    SPIScreenManager::getInstance().loop();
    POWER_MANAGER.setup();
    interactiveRuntimeInitialized = true;
}

MainRuntimeState MainStateMachine::resolveNormalStartupState() const
{
    /* A valid READY is an explicit host commit and therefore outranks even
     * the manual USB ISP flag. No other staging record triggers an update. */
    if (CH585_FIRMWARE_UPDATE.hasReadyStagedImage()) {
        return MainRuntimeState::Ch585BridgeUpdate;
    }

    if (CH585_UPDATE_MODE.isManualIspActive()) {
        return MainRuntimeState::Ch585UsbIsp;
    }

    BootMode bootMode = STORAGE_MANAGER.getBootMode();
#if WEBCONFIG_TEST_FORCE_BOOT
    bootMode = BootMode::BOOT_MODE_WEB_CONFIG;
    APP_STAGE("A12", "temporary WebConfig bring-up override active");
#endif
#if RF24G_SPI_TEST_FORCE_RF24G
    bootMode = BootMode::BOOT_MODE_INPUT;
#endif
    if (!isValidBootMode(bootMode)) return MainRuntimeState::SafeRecovery;
    if (bootMode == BootMode::BOOT_MODE_INPUT) return MainRuntimeState::Input;
    if (bootMode == BootMode::BOOT_MODE_WEB_CONFIG) {
        return MainRuntimeState::WebConfig;
    }
    return MainRuntimeState::Calibration;
}

bool MainStateMachine::enterState(MainRuntimeState selected)
{
    BaseState* next = stateFor(selected);
    if (state != nullptr) state->exit();
    state = next;
    currentState = selected;
    APP_STAGE("A13", "entering top-level runtime state=%u",
              static_cast<unsigned>(selected));
    if (state->enter()) return true;

    if (selected == MainRuntimeState::SafeRecovery) return false;
    state->exit();
    if (!interactiveRuntimeInitialized) initializeInteractiveRuntime();
    state = stateFor(MainRuntimeState::SafeRecovery);
    currentState = MainRuntimeState::SafeRecovery;
    return state->enter();
}

bool MainStateMachine::requestTransition(MainRuntimeState next)
{
    if (next == currentState) return true;
    if (next != MainRuntimeState::Ch585BridgeUpdate &&
        !interactiveRuntimeInitialized) {
        initializeInteractiveRuntime();
    }
    return enterState(next);
}

void MainStateMachine::requestReset()
{
    resetPending = true;
}

extern "C" void MainRuntime_RequestReset(void)
{
    APP_STAGE("A14", "runtime reset requested by caller=0x%08lX",
              (unsigned long)(uintptr_t)__builtin_return_address(0));
    MAIN_STATE_MACHINE.requestReset();
}

void MainStateMachine::serviceSharedRuntime()
{
    if (!interactiveRuntimeInitialized) return;
    static bool traceFirstPass = true;
    if (traceFirstPass) APP_STAGE("RT0", "first shared-runtime pass begin");
    BOARD_MODE.update(HAL_GetTick());
    if (traceFirstPass) APP_STAGE("RT1", "board-mode service complete");
    if (currentState == MainRuntimeState::Input ||
        currentState == MainRuntimeState::WebConfig) {
        CONNECTION_MANAGER.loop();
    }
    if (traceFirstPass) APP_STAGE("RT2", "connection service complete");
    POWER_MANAGER.loop();
    if (traceFirstPass) APP_STAGE("RT3", "power service complete");
    SPIScreenManager::getInstance().loop();
    if (currentState == MainRuntimeState::Input) {
        INPUT_STATE.serviceLeds();
    }
    if (traceFirstPass) {
        APP_STAGE("RT4", "screen and LED frame service complete");
        traceFirstPass = false;
    }
}

void MainStateMachine::setup()
{
    APP_STAGE("A10", "top-level runtime dispatcher active");

    /* READY is the only condition inspected before Logger/Storage/screen.
     * This keeps BridgeUpdate an isolated peer state instead of an early-boot
     * side path hidden outside the main state machine. */
    if (CH585_FIRMWARE_UPDATE.hasReadyStagedImage()) {
        (void)enterState(MainRuntimeState::Ch585BridgeUpdate);
    } else {
        initializeInteractiveRuntime();
        (void)enterState(resolveNormalStartupState());
    }

    while (true) {
        if (state != nullptr) state->tick();
        serviceSharedRuntime();
        if (resetPending) {
            resetPending = false;
            APP_STAGE("A15", "executing requested runtime reset");
            if (state != nullptr) state->exit();
            if (interactiveRuntimeInitialized) Logger_Flush();
            NVIC_SystemReset();
        }
    }
}
