#include "rf_sleep_recovery.hpp"
#include "rf_bridge_port.hpp"
#include "usb_board_link_port.hpp"
#include "usb_board_link.hpp"
#include "ch585_role_bootstrap.hpp"
#include "connection_manager.hpp"
#include "sleep_diagnostics.hpp"
#include "stm32h7xx_hal.h"

bool RfSleepRecovery::park() {
    CONNECTION_MANAGER.onRfPowerRemovedForSleep(); // block every regular producer/poller first
    // A failed stop keeps the supply on. Retry cleanup later, never cut power
    // with an unconfirmed SPI/DMA owner or a driven bootstrap port.
    if (!USBBoardLinkPort_TryShutdown() || !RFBridgePort_TryShutdownForSleep()) return false;
    USB_BOARD_LINK.shutdown();
    CONNECTION_MANAGER.resetRfSleepSession();
    transport_.resetSession();
    CH585_ROLE_BOOTSTRAP.shutdown();
    SleepDiagnostics_Record(SleepStage::PortParked);
    return true;
}

bool RfSleepRecovery::suspend() {
    state_ = RfSleepState::Off;
    return park();
}

void RfSleepRecovery::begin(uint16_t rate) {
    requested_ = rate ? rate : 1000u;
    startAttempt();
}

void RfSleepRecovery::startAttempt() {
    ++g_sleepDiagnostics.radioAttempts;
    if (!park()) { fail(RfSleepError::PortStop, HAL_GetTick()); return; }
    fallback_ = requestSent_ = verified_ = neutralSent_ = false;
    rate_ = requested_;
    CONNECTION_MANAGER.setRfSleepRecoveryError(false);
    CH585_ROLE_BOOTSTRAP.setSelector([](Ch585Role role) {
        return role == Ch585Role::Rf && USBBoardLinkPort_SelectRfRoleOnce();
    });
    CH585_ROLE_BOOTSTRAP.beginRfSleepResume();
    state_ = RfSleepState::PowerWait;
}

void RfSleepRecovery::configure(uint32_t now) {
    RFCommandTransaction::beginScheduled(window_, now);
    requestSent_ = verified_ = neutralSent_ = false;
    state_ = RfSleepState::ConfigureRate;
    SleepDiagnostics_Record(SleepStage::RadioConfigure);
}

void RfSleepRecovery::fail(RfSleepError error, uint32_t now) {
    // Even if cleanup fails, ownership stays here and normal input cannot
    // touch SPI. Local input/render/lighting remain entirely independent.
    if (!park()) error = RfSleepError::PortStop;
    state_ = RfSleepState::RetryWait;
    since_ = now;
    CONNECTION_MANAGER.setRfSleepRecoveryError(true);
    SleepDiagnostics_Record(SleepStage::RadioRetry, static_cast<uint32_t>(error));
}

void RfSleepRecovery::cancel() {
    RFBridgePort_CancelRecoveryIo();
    state_ = RfSleepState::Off; // the mode/reset owner performs regular teardown
}

void RfSleepRecovery::service(uint32_t now) {
    if (state_ == RfSleepState::Off || state_ == RfSleepState::Ready) return;
    if (state_ == RfSleepState::RetryWait) {
        if (now - since_ >= 10000u) startAttempt();
        return;
    }
    if (state_ == RfSleepState::PowerWait || state_ == RfSleepState::BootWait ||
        state_ == RfSleepState::SelectRole) {
        const auto result = CH585_ROLE_BOOTSTRAP.serviceRfSleepResume();
        if (result == Ch585ResumeResult::Failed) { fail(RfSleepError::Role, HAL_GetTick()); return; }
        if (result == Ch585ResumeResult::Pending) {
            switch (CH585_ROLE_BOOTSTRAP.state()) {
            case Ch585BootstrapState::Off: state_ = RfSleepState::PowerWait; break;
            case Ch585BootstrapState::Booting: state_ = RfSleepState::BootWait; break;
            default: state_ = RfSleepState::SelectRole; break;
            }
            return;
        }
        // ROLE_SELECTED acknowledges the selector, not completion of RF_Init.
        // Keep NSS high while the selector releases SPI and the RF port emits
        // its separate 100-ms boot-ready pulse. No SPI clocks during that pulse.
        state_ = RfSleepState::ApplicationWait;
        since_ = HAL_GetTick();
        SleepDiagnostics_Record(SleepStage::RadioApplicationWait);
        return;
    }
    if (state_ == RfSleepState::ApplicationWait) {
        if (now - since_ < applicationSettleMs) return;
        // Release the bootstrap handle before initializing RF SPI4/DMA.
        if (!USBBoardLinkPort_TryShutdown()) { fail(RfSleepError::PortStop, HAL_GetTick()); return; }
        USB_BOARD_LINK.shutdown();
        if (!RFBridgePort_RecoveryBegin()) { fail(RfSleepError::PortStart, HAL_GetTick()); return; }
        configure(HAL_GetTick());
        return;
    }

    // Regular RF polling is paused while this owner holds SPI4. Advance only
    // the in-memory reliable-event queue; a zero drain limit performs no I/O.
    transport_.serviceEvents(0u);
    uint8_t frame[64] = {};
    uint16_t length = sizeof(frame);
    const auto receive = RFBridgePort_RecoveryRead(frame, &length, now);
    if (receive == RFPortStep::Error) { fail(RfSleepError::Receive, now); return; }
    if (receive == RFPortStep::Complete) {
        g_sleepDiagnostics.lastRadioFrameEvent = length > 1u ? frame[1] : 0u;
        g_sleepDiagnostics.lastRadioFramePayloadBytes = length > 2u ? frame[2] : 0u;
        if (!transport_.acceptRecoveryFrame(frame, length)) { fail(RfSleepError::FrameRejected, now); return; }
        const auto& status = transport_.getStatus();
        if (state_ == RfSleepState::VerifyStatus && requestSent_ &&
            transport_.receivedStatusGeneration() != generation_) {
            requestSent_ = false;
            verified_ = status.lastEvent == 0x81u && status.lastCommandTag == 0x01u &&
                        status.lastResult == 0u && status.rateHz == rate_;
            if (verified_) SleepDiagnostics_Record(SleepStage::RadioVerify);
        }
    }
    if (state_ == RfSleepState::ConfigureRate) {
        const uint8_t args[] = {static_cast<uint8_t>(rate_), static_cast<uint8_t>(rate_ >> 8)};
        RFCommandTransaction::stepScheduled(window_, 0x05u, args, sizeof(args), now);
        if (now - window_.started < 100u) return;
        if (!window_.sent) {
            if (!fallback_ && rate_ != 1000u) {
                fallback_ = true;
                rate_ = 1000u;
                configure(now);
            } else fail(RfSleepError::RateSend, now);
            return;
        }
        state_ = RfSleepState::VerifyStatus;
        since_ = now;
        lastPoll_ = now - 50u;
        return;
    }
    if (verified_) {
        if (!neutralSent_ && RFBridgePort_RecoveryIdle()) {
            const GamepadState neutral = {};
            neutralSent_ = transport_.sendInput(neutral, 0u);
        }
        // Do not let the first latest-state packet overwrite queued neutral.
        if (neutralSent_ && RFBridgePort_IsInputIdle()) {
            CONNECTION_MANAGER.completeRfSleepRecovery(transport_, rate_);
            state_ = RfSleepState::Ready;
            SleepDiagnostics_Record(SleepStage::RadioReady);
            return;
        }
    } else if (!requestSent_ && RFBridgePort_RecoveryIdle() && now - lastPoll_ >= 50u) {
        // Existing GET_STATUS snapshot format; no synthesized RATE_APPLIED.
        const uint8_t request[] = {0xa5u, 0x01u, 0x00u, 0xa6u};
        generation_ = transport_.receivedStatusGeneration();
        if (RFBridgePort_RecoverySend(request, sizeof(request))) {
            requestSent_ = true;
            lastPoll_ = now;
        }
    }
    if (now - since_ >= 500u) {
        if (!verified_ && !fallback_ && rate_ != 1000u) {
            fallback_ = true;
            rate_ = 1000u;
            // Abort a partial response before starting a new transaction window.
            RFBridgePort_CancelRecoveryIo();
            configure(now);
        } else fail(verified_ ? RfSleepError::Neutral : RfSleepError::StatusTimeout, now);
    }
}
