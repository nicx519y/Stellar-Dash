#include "main_state_machine.hpp"
#include "main_runtime_control.hpp"

#include "adc_btns/adc_manager.hpp"
#include "board_cfg.h"
#include "board_mode.hpp"
#include "connection_mode_config.hpp"
#include "board_power.hpp"
#include "ch585_firmware_update.hpp"
#include "release_installer.hpp"
#include "config.hpp"
#include "ch585_role_bootstrap.hpp"
#include "connection_manager.hpp"
#include "power_manager.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "states/calibration_state.hpp"
#include "states/ch585_bridge_update_state.hpp"
#include "states/safe_recovery_state.hpp"
#include "states/tx_isp_state.hpp"
#include "states/battery_protection_state.hpp"
#include "system_logger.h"
#include "fault_log_api.h"
#include "battery_test_log.h"
#include "usb_board_link.hpp"
#include "system_sleep_manager.hpp"
#include "boot_profile.h"
#pragma GCC optimize("Os")


namespace {

static_assert(static_cast<unsigned>(MainRuntimeState::BatteryProtection) + 1u == 7u,
              "The STM32 top-level runtime must contain exactly seven states");

static bool isValidBootMode(BootMode mode)
{
    return mode == BootMode::BOOT_MODE_INPUT ||
           mode == BootMode::BOOT_MODE_WEB_CONFIG ||
           mode == BootMode::BOOT_MODE_CALIBRATION ||
           mode == BootMode::BOOT_MODE_TX_ISP;
}

static bool ispEntryBlocked()
{
    const auto status = CH585_FIRMWARE_UPDATE.status();
    return RELEASE_INSTALLER.busy() || CH585_FIRMWARE_UPDATE.isPending() ||
           status == Ch585FirmwareUpdateStatus::Receiving ||
           status == Ch585FirmwareUpdateStatus::Scheduled ||
           status == Ch585FirmwareUpdateStatus::Programming;
}

} // namespace

BaseState* MainStateMachine::stateFor(MainRuntimeState selected) const
{
    switch (selected) {
        case MainRuntimeState::Input: return &INPUT_STATE;
        case MainRuntimeState::WebConfig: return &WEB_CONFIG_STATE;
        case MainRuntimeState::Calibration: return &CALIBRATION_STATE;
        case MainRuntimeState::Ch585BridgeUpdate:
            return &CH585_BRIDGE_UPDATE_STATE;
        case MainRuntimeState::SafeRecovery: return &SAFE_RECOVERY_STATE;
        case MainRuntimeState::TxIsp: return &TX_ISP_STATE;
        case MainRuntimeState::BatteryProtection: return &BATTERY_PROTECTION_STATE;
    }
    return &SAFE_RECOVERY_STATE;
}

void MainStateMachine::initializeInteractiveRuntime(bool overlapInputStartup)
{
    if (interactiveRuntimeInitialized) return;

    XoraLog_Init();
    XoraLog_Service(1); // Before transport, screen or configuration writers start.
    if (BatteryTest_Init) BatteryTest_Init();
    if (BatteryTest_Service) BatteryTest_Service(1);
    const LogResult logResult = Logger_Init(false, LOG_LEVEL_DEBUG);
    APP_STAGE(logResult == LOG_RESULT_SUCCESS ? "A08" : "A08E",
              "persistent logger initialization result=%d", logResult);

    BOARD_POWER.enterRecoveryUiState();
    if (RELEASE_INSTALLER.protectConfiguration()) {
        // Never let the normal default/migration path overwrite configuration
        // while determining the result of an activated installation.
        const bool readable = ConfigUtils::fromStorage(STORAGE_MANAGER.config);
        RELEASE_INSTALLER.verifyStartup(readable);
    } else {
        STORAGE_MANAGER.initConfig();
    }
    APP_STAGE("A12", "configuration loaded: boot mode=%u",
              static_cast<unsigned>(STORAGE_MANAGER.getBootMode()));
    const bool maintenance = resolveNormalStartupState() == MainRuntimeState::WebConfig;
    BOARD_MODE.setup(maintenance || ConnectionConfig::selected(STORAGE_MANAGER.config) == CONNECTION_MODE_USB
        ? BoardMode::Usb : BoardMode::Rf);
    APP_STAGE("A11", "software runtime mode selected: mode=%u", static_cast<unsigned>(BOARD_MODE.current()));

    /* Every interactive state gets the independent recovery display and
     * power telemetry. BridgeUpdate is deliberately dispatched before this
     * point so writable QSPI can never overlap screen asset reads. */
    if (overlapInputStartup && resolveNormalStartupState() == MainRuntimeState::Input &&
        STORAGE_MANAGER.getInputMode() != INPUT_MODE_CONFIG && BOARD_MODE.isUsbStartupSafe()) {
        (void)CH585_ROLE_BOOTSTRAP.prepareUsbStartup();
    }
    SPIScreenManager::getInstance().setup();
    SPIScreenManager::getInstance().loop();
    POWER_MANAGER.setup();
    interactiveRuntimeInitialized = true;
}

MainRuntimeState MainStateMachine::resolveNormalStartupState() const
{
    if (RELEASE_INSTALLER.failed()) return MainRuntimeState::SafeRecovery;
    /* A valid READY is the only CH585 journal state that diverts startup.
     * Interrupted or failed transactions remain diagnostic records and must
     * never lock the controller out of its normal interactive runtime. */
    if (CH585_FIRMWARE_UPDATE.hasReadyStagedImage()) {
        return MainRuntimeState::Ch585BridgeUpdate;
    }

    BootMode bootMode = STORAGE_MANAGER.getBootMode();
    // Like WebConfig, ISP is selected by the persisted bootMode. Resolve it
    // before test overrides and before normal TX pre-start/handshake.
    if (bootMode == BootMode::BOOT_MODE_TX_ISP) {
        return MainRuntimeState::TxIsp;
    }
    if (XoraLog_Restricted()) return MainRuntimeState::WebConfig;
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
    if (selected == MainRuntimeState::TxIsp) {
        const uint16_t returnMode = STORAGE_MANAGER.config.screenControl.txIspReturnBootMode;
        ispReturnState = returnMode == BootMode::BOOT_MODE_WEB_CONFIG
            ? MainRuntimeState::WebConfig : (returnMode == BootMode::BOOT_MODE_INPUT
            ? MainRuntimeState::Input : MainRuntimeState::SafeRecovery);
    }
    // Gate every entry path, including persisted boot mode and test overrides,
    // before WebConfig can initialize its USB/maintenance runtime.
    if (selected == MainRuntimeState::WebConfig &&
        !BOARD_MODE.isWebConfigAllowed()) {
        APP_STAGE("A13", "WebConfig blocked by invalid runtime selection; entering Input");
        STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
        selected = MainRuntimeState::Input;
    }
    if (selected != MainRuntimeState::Input && CH585_ROLE_BOOTSTRAP.hasPreparedUsbStartup()) {
        CH585_ROLE_BOOTSTRAP.shutdown();
    }
    /* v4 identifies the effective mode after the runtime WebConfig gate.
     * The span includes any SafeRecovery fallback performed by this call. */
    BP_APP_SCOPE((selected == MainRuntimeState::TxIsp || selected == MainRuntimeState::BatteryProtection) ? BP_APP_STATE_ENTER :
                 BP_APP_STATE_INPUT + static_cast<unsigned>(selected));
    BaseState* next = stateFor(selected);
    if (state != nullptr) state->exit();
    state = next;
    currentState = selected;
    BOARD_MODE.setup(selected == MainRuntimeState::WebConfig ||
        ConnectionConfig::selected(STORAGE_MANAGER.config) == CONNECTION_MODE_USB
            ? BoardMode::Usb : BoardMode::Rf);
    APP_STAGE("A13", "entering top-level runtime state=%u",
              static_cast<unsigned>(selected));
    if (state->enter()) return true;

    if (selected == MainRuntimeState::SafeRecovery) return false;
    state->exit();
    if (!interactiveRuntimeInitialized) initializeInteractiveRuntime();
    const MainRuntimeState fallback = XoraLog_Restricted() && selected != MainRuntimeState::WebConfig
        ? MainRuntimeState::WebConfig : MainRuntimeState::SafeRecovery;
    if(fallback==MainRuntimeState::WebConfig) BOARD_MODE.setup(BoardMode::Usb);
    state = stateFor(fallback);
    currentState = fallback;
    return state->enter();
}

bool MainStateMachine::requestTransition(MainRuntimeState next)
{
    if (currentState == MainRuntimeState::BatteryProtection) return false;
    if (next == currentState) return true;
    if (next != MainRuntimeState::Ch585BridgeUpdate &&
        !interactiveRuntimeInitialized) {
        initializeInteractiveRuntime();
    }
    return enterState(next);
}

void MainStateMachine::requestReset()
{
    // ISP writes belong to the external tool. No timed/system reset may interrupt them.
    if (currentState == MainRuntimeState::TxIsp || currentState == MainRuntimeState::BatteryProtection) return;
    INPUT_STATE.cancelSleepRecovery();
    resetPending = true;
}

bool MainStateMachine::requestTxIsp(bool enabled)
{
    ispTransitionFailed = false;
    if (resetPending || transitionPending) return false;
    if (enabled) {
        if (currentState == MainRuntimeState::TxIsp) return true;
        if ((currentState != MainRuntimeState::Input &&
             currentState != MainRuntimeState::WebConfig &&
             currentState != MainRuntimeState::SafeRecovery) ||
            ispEntryBlocked() || POWER_MANAGER.isLowBattery()) return false;
        ispReturnState = currentState;
        pendingState = MainRuntimeState::TxIsp;
    } else {
        if (currentState != MainRuntimeState::TxIsp) return false;
        pendingState = ispReturnState;
    }
    transitionPending = true;
    return true;
}

void MainStateMachine::servicePendingTransition()
{
    if (!transitionPending) return;
    transitionPending = false;
    // Recheck ownership before stopping the old state or cycling the TX supply.
    if (pendingState == MainRuntimeState::TxIsp && (ispEntryBlocked() || POWER_MANAGER.isLowBattery())) {
        ispTransitionFailed = true;
        return;
    }
    // Commit after the LCD frame. A failed journal save must not cycle TX power
    // or leave ISP; Input sampling must be stopped during QSPI indirect access.
    const bool entering = pendingState == MainRuntimeState::TxIsp;
    const BootMode previousBootMode = STORAGE_MANAGER.getBootMode();
    const uint16_t previousReturnMode = STORAGE_MANAGER.config.screenControl.txIspReturnBootMode;
    const bool inputWasRunning = currentState == MainRuntimeState::Input &&
        INPUT_STATE.suspendInputPipelineForStorage();
    if (entering) {
        STORAGE_MANAGER.config.screenControl.txIspReturnBootMode =
            ispReturnState == MainRuntimeState::WebConfig ? static_cast<uint16_t>(BootMode::BOOT_MODE_WEB_CONFIG) :
            ispReturnState == MainRuntimeState::Input ? static_cast<uint16_t>(BootMode::BOOT_MODE_INPUT) : 0u;
        STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_TX_ISP);
    } else {
        STORAGE_MANAGER.setBootMode(static_cast<BootMode>(previousReturnMode));
        STORAGE_MANAGER.config.screenControl.txIspReturnBootMode = 0u;
    }
    if (!STORAGE_MANAGER.saveConfig()) {
        STORAGE_MANAGER.setBootMode(previousBootMode);
        STORAGE_MANAGER.config.screenControl.txIspReturnBootMode = previousReturnMode;
        ispTransitionFailed = true;
        if (!INPUT_STATE.resumeInputPipelineAfterStorage(inputWasRunning))
            (void)requestTransition(MainRuntimeState::SafeRecovery);
        return;
    }
    (void)requestTransition(pendingState);
    ispTransitionFailed = currentState != pendingState;
}

extern "C" bool MainRuntime_RequestTxIsp(bool enabled)
{
    return MAIN_STATE_MACHINE.requestTxIsp(enabled);
}

extern "C" bool MainRuntime_IsTxIspActive(void)
{
    return MAIN_STATE_MACHINE.current() == MainRuntimeState::TxIsp;
}

extern "C" void MainRuntime_RequestBatteryRecovery(void)
{
    MAIN_STATE_MACHINE.requestBatteryRecovery();
}

extern "C" bool MainRuntime_IsInputActive(void)
{
    return MAIN_STATE_MACHINE.current() == MainRuntimeState::Input;
}

void MainStateMachine::serviceBatteryProtection()
{
    if (currentState == MainRuntimeState::BatteryProtection) {
        if (batteryRecoveryPending) {
            batteryRecoveryPending = false;
            if (POWER_MANAGER.acknowledgeBatteryRecovery())
                (void)enterState(batteryReturnState);
        }
        return;
    }
    if (!interactiveRuntimeInitialized || !POWER_MANAGER.batteryProtectionRequired() ||
        currentState == MainRuntimeState::TxIsp || currentState == MainRuntimeState::Ch585BridgeUpdate ||
        RELEASE_INSTALLER.defersBatteryProtection() || FirmwareManager::GetInstance()->HasPendingUpgradeWrites() ||
        CH585_FIRMWARE_UPDATE.isPending() ||
        CH585_FIRMWARE_UPDATE.status() == Ch585FirmwareUpdateStatus::Receiving) return;
    batteryReturnState = currentState == MainRuntimeState::WebConfig ? MainRuntimeState::WebConfig :
        currentState == MainRuntimeState::SafeRecovery ? MainRuntimeState::SafeRecovery : MainRuntimeState::Input;
    // All config/flash operations are synchronous on this main thread. This
    // boundary runs after the current transaction/frame has returned.
    transitionPending = resetPending = batteryRecoveryPending = false;
    if (currentState == MainRuntimeState::Input) INPUT_STATE.prepareBatteryProtection();
    (void)enterState(MainRuntimeState::BatteryProtection);
}

extern "C" bool MainRuntime_TxIspTransitionFailed(void)
{
    return MAIN_STATE_MACHINE.txIspTransitionFailed();
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
    if (traceFirstPass) APP_STAGE("RT1", "board-mode service complete");
    if (currentState == MainRuntimeState::Input ||
        currentState == MainRuntimeState::WebConfig) {
        CONNECTION_MANAGER.loop();
    }
    if (traceFirstPass) APP_STAGE("RT2", "connection service complete");
    POWER_MANAGER.loop();
    if (BatteryTest_Tick) BatteryTest_Tick();
    serviceBatteryProtection();
    XoraLog_Tick(HAL_GetTick());
    if (currentState == MainRuntimeState::WebConfig && !USB_BOARD_LINK.isDeviceMounted() && !ispEntryBlocked())
        XoraLog_Service(1); // Host detached; no user RPC can contend with QSPI.
    SystemSleep_Service(currentState == MainRuntimeState::Input, resetPending);
    // Observe sleep entry/exit before the first STOP interval, so that interval
    // is not charged to the preceding awake state. This adds no I2C sampling.
    if (BatteryTest_Tick) BatteryTest_Tick();
    if (traceFirstPass) APP_STAGE("RT3", "power service complete");
    // Service the previous LCD transfer before allowing the UI to submit a
    // new frame. Otherwise continuous rendering can starve a confirmed clear
    // (or queued records) until its request times out with the old elapsed time.
    if (BatteryTest_NeedsStorage && BatteryTest_NeedsStorage() && !ispEntryBlocked() &&
        !FirmwareManager::GetInstance()->HasPendingUpgradeWrites() &&
        BatteryTest_DisplayReady && BatteryTest_DisplayReady() &&
        (currentState == MainRuntimeState::Input ||
         (currentState == MainRuntimeState::BatteryProtection && BATTERY_PROTECTION_STATE.storageReady()) ||
         (currentState == MainRuntimeState::WebConfig && !USB_BOARD_LINK.isDeviceMounted()))) {
        const auto pauseStarted=HAL_GetTick();
        const bool input = currentState == MainRuntimeState::Input;
        const bool running = input && INPUT_STATE.suspendInputPipelineForStorage();
        if (ADC_MANAGER.isSamplingHardwareStopped() && BatteryTest_Service) BatteryTest_Service(1);
        if (input && !INPUT_STATE.resumeInputPipelineAfterStorage(running))
            (void)requestTransition(MainRuntimeState::SafeRecovery);
        if(BatteryTest_StoragePause) BatteryTest_StoragePause(HAL_GetTick()-pauseStarted);
    }
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
    if (RELEASE_INSTALLER.bootPending() || CH585_FIRMWARE_UPDATE.hasReadyStagedImage()) {
        (void)enterState(MainRuntimeState::Ch585BridgeUpdate);
    } else {
        initializeInteractiveRuntime(true);
        (void)enterState(resolveNormalStartupState());
    }

    /* Records dispatcher completion, not USB enumeration/RF link readiness. */
    BootProfile_AppComplete();
    while (true) {
        if (interactiveRuntimeInitialized && currentState != MainRuntimeState::TxIsp &&
            currentState != MainRuntimeState::BatteryProtection)
            RELEASE_INSTALLER.poll();
        serviceBatteryProtection();
        if (state != nullptr) state->tick();
        serviceSharedRuntime();
        servicePendingTransition();
        if (resetPending) {
            resetPending = false;
            APP_STAGE("A15", "executing requested runtime reset");
            if (state != nullptr) state->exit();
            if (interactiveRuntimeInitialized) Logger_Flush();
            NVIC_SystemReset();
        }
        if (currentState == MainRuntimeState::BatteryProtection) BATTERY_PROTECTION_STATE.idle();
        else SystemSleep_Idle();
    }
}
