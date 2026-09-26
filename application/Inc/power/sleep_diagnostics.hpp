#pragma once
#include <stdint.h>

enum class SleepStage : uint32_t {
    Awake, Preparing, PortParked, StopEnter, StopReturn, LocalRestore,
    LocalReady, RadioPowerWait, RadioPowerOn, RadioSelectRole,
    RadioRoleReady, RadioConfigure, RadioVerify, RadioReady, RadioRetry, Failure,
    RadioApplicationWait
};
// Ordinary RAM only; powerOn denotes an enable request, never measured voltage.
struct SleepDiagnostics {
    uint32_t stage, changedAt, stopEntries, stopReturns, wakePins;
    uint32_t radioAttempts, lastError;
    uint32_t lastRadioFrameEvent, lastRadioFramePayloadBytes;
};
extern volatile SleepDiagnostics g_sleepDiagnostics;
void SleepDiagnostics_Record(SleepStage stage, uint32_t error = 0u);
