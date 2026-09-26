#pragma once
#include <cstdint>
#include <deque>
#include <cassert>
#define RF_TRANSPORT_HPP
#define RF_COMMAND_TRANSACTION_HPP
#include "sleep_diagnostics.hpp"

inline uint32_t testNow = 0, localIterations = 0, powerOns = 0, powerOffs = 0, neutralCount = 0;
inline bool parkOk = true, roleOk = true, beginOk = true, sendOk = true;
inline bool autoReply = true, wrongRate = false, receiverOnline = true, owner = false, published = false;
inline uint16_t wireRate = 1000, publishedRate = 0;
inline uint32_t queryCount = 0;
inline uint32_t applicationReadyAt = 0, portBegins = 0, portReads = 0;
inline void assertApplicationReady() {
    assert(static_cast<int32_t>(testNow - applicationReadyAt) >= 0);
}
struct Reply { uint16_t rate; bool physicalStatus; };
inline std::deque<Reply> replies;
inline uint32_t HAL_GetTick() { return testNow; }
inline volatile SleepDiagnostics g_sleepDiagnostics = {};
inline void SleepDiagnostics_Record(SleepStage stage, uint32_t error) {
    g_sleepDiagnostics.stage = static_cast<uint32_t>(stage);
    if (error) g_sleepDiagnostics.lastError = error;
}
struct GamepadState {};
struct RFModuleStatus {
    uint16_t rateHz = 1000;
    uint8_t lastEvent = 0, lastCommandTag = 0, lastResult = 0;
    bool connected = false;
};
class RFTransport {
    RFModuleStatus status_{};
    uint32_t generation_ = 0;
public:
    void resetSession() { status_ = {}; generation_ = 0; replies.clear(); }
    uint32_t receivedStatusGeneration() const { return generation_; }
    const RFModuleStatus& getStatus() const { return status_; }
    uint8_t serviceEvents(uint8_t drainLimit) { assert(drainLimit == 0u); return 0u; }
    bool acceptRecoveryFrame(const uint8_t* f, uint16_t) {
        status_.rateHz = f[0] | (f[1] << 8);
        status_.lastEvent = f[2] ? 0x81 : 0x83;
        status_.lastCommandTag = f[2] ? 1 : 5;
        status_.connected = receiverOnline;
        if (f[2]) ++generation_;
        return true;
    }
    bool sendInput(const GamepadState&, uint32_t) { ++neutralCount; return sendOk; }
};
struct RFScheduledCommand { uint32_t started = 0; bool sent = false; };
struct RFCommandTransaction {
    static void beginScheduled(RFScheduledCommand& w, uint32_t now) { w = {now, false}; }
    static void stepScheduled(RFScheduledCommand& w, uint8_t, const uint8_t* args, uint8_t, uint32_t) {
        assertApplicationReady();
        wireRate = args[0] | (args[1] << 8); w.sent = sendOk;
    }
};
enum class RFPortStep { Pending, Complete, Error };
inline bool RFBridgePort_TryShutdownForSleep() { return parkOk; }
inline bool USBBoardLinkPort_TryShutdown() { return parkOk; }
inline bool USBBoardLinkPort_SelectRfRoleOnce() { return roleOk; }
inline bool RFBridgePort_RecoveryBegin() { assertApplicationReady(); ++portBegins; return beginOk; }
inline bool RFBridgePort_RecoveryIdle() { return replies.empty(); }
inline bool RFBridgePort_IsInputIdle() { return true; }
inline void RFBridgePort_CancelRecoveryIo() {}
inline bool RFBridgePort_RecoverySend(const uint8_t*, uint16_t) {
    assertApplicationReady();
    assert(owner);
    ++queryCount;
    if (autoReply) replies.push_back({static_cast<uint16_t>(wrongRate && wireRate != 1000 ? 500 : wireRate), true});
    return sendOk;
}
inline RFPortStep RFBridgePort_RecoveryRead(uint8_t* f, uint16_t* len, uint32_t) {
    assertApplicationReady(); ++portReads;
    if (replies.empty()) return RFPortStep::Pending;
    const auto reply = replies.front(); replies.pop_front();
    f[0] = reply.rate; f[1] = reply.rate >> 8; f[2] = reply.physicalStatus; *len = 3;
    return RFPortStep::Complete;
}
enum class Ch585Role { Rf };
enum class Ch585BootstrapState { Off, Booting, Selecting, Locked };
enum class Ch585ResumeResult { Pending, Ready, Failed };
inline bool UsbBoardLink_SelectRoleCallback(Ch585Role) { return roleOk; }
struct FakeBootstrap {
    uint32_t since = 0;
    Ch585BootstrapState phase = Ch585BootstrapState::Off;
    void setSelector(bool (*)(Ch585Role)) {}
    void shutdown() { ++powerOffs; phase = Ch585BootstrapState::Off; }
    void beginRfSleepResume() { since = testNow; }
    Ch585BootstrapState state() { return phase; }
    Ch585ResumeResult serviceRfSleepResume() {
        if (phase == Ch585BootstrapState::Off) {
            if (testNow - since < 20) return Ch585ResumeResult::Pending;
            ++powerOns; phase = Ch585BootstrapState::Booting; since = testNow;
        }
        if (testNow - since < 720) return Ch585ResumeResult::Pending;
        phase = Ch585BootstrapState::Selecting;
        if (roleOk) applicationReadyAt = testNow + 100u; // separate RF W_INT ready pulse
        return roleOk ? Ch585ResumeResult::Ready : Ch585ResumeResult::Failed;
    }
};
inline FakeBootstrap CH585_ROLE_BOOTSTRAP;
struct FakeLink { void shutdown() {} };
inline FakeLink USB_BOARD_LINK;
struct FakeConnection {
    void onRfPowerRemovedForSleep() { owner = true; published = false; }
    void resetRfSleepSession() {}
    void setRfSleepRecoveryError(bool) {}
    void completeRfSleepRecovery(const RFTransport&, uint16_t rate) {
        assert(neutralCount > 0); owner = false; published = true; publishedRate = rate;
    }
};
inline FakeConnection CONNECTION_MANAGER;
