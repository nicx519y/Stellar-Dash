#pragma once
#include "rf_transport.hpp"
#include "rf_command_transaction.hpp"

enum class RfSleepState { Off, PowerWait, BootWait, SelectRole, ConfigureRate, VerifyStatus, Ready, RetryWait, ApplicationWait };
enum class RfSleepError : uint32_t { None, PortStop, Role, PortStart, Receive, RateSend, StatusTimeout, Neutral, FrameRejected };

// Sole SPI4 owner between RF sleep preparation and a validated new session.
// Never owns local ADC, LEDs, LCD or their failure paths.
class RfSleepRecovery {
public:
    // The RF application emits a 100-ms W_INT ready pulse *after* ROLE_SELECTED.
    // Match the existing 150-ms USB handoff fallback; this is not RF data.
    static constexpr uint32_t applicationSettleMs = 150u;
    static RfSleepRecovery& instance() { static RfSleepRecovery value; return value; }
    bool suspend();
    void begin(uint16_t rate);
    void service(uint32_t now);
    void cancel();
    RfSleepState state() const { return state_; }
    uint16_t rate() const { return rate_; }
private:
    bool park();
    void startAttempt();
    void configure(uint32_t now);
    void fail(RfSleepError error, uint32_t now);
    RfSleepState state_ = RfSleepState::Off;
    RFTransport transport_;
    RFScheduledCommand window_;
    uint16_t rate_ = 1000, requested_ = 1000;
    uint32_t since_ = 0, generation_ = 0, lastPoll_ = 0;
    bool fallback_ = false, requestSent_ = false, verified_ = false, neutralSent_ = false;
};
#define RF_SLEEP_RECOVERY RfSleepRecovery::instance()
