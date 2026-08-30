#ifndef MONITOR_TELEMETRY_HPP
#define MONITOR_TELEMETRY_HPP

#include <stdint.h>

#include "enums.hpp"

struct MonitorTelemetrySnapshot {
    uint32_t lastUpdateUs;
    uint32_t totalReports;
    uint32_t usbReportsCompleted;
    uint32_t rfTransferOk;
    uint32_t rfTransferFail;
    uint32_t errorCount;

    uint32_t latestSampleToCh585SubmitUs;
    uint32_t latestSampleToRfSubmitUs;
    uint32_t avgSampleToCh585SubmitUs;
    uint32_t avgSampleToRfSubmitUs;
    uint32_t latestAdcConversionUs;
    uint32_t latestInputProcessingUs;
    uint32_t latestReportSubmitUs;

    uint16_t targetRateHz;
    uint8_t connectionMode;
    uint8_t linkState;
    uint16_t lastUsbReportLen;
    uint8_t lastRfCmd;
    uint8_t lastRfPayloadLen;
};

struct MonitorTelemetryFrameV1 {
    uint32_t magic; /* "MON1" */
    uint32_t seq;
    uint32_t totalReports;
    uint32_t usbReportsCompleted;
    uint16_t targetRateHz;
    uint16_t lastUsbReportLen;
    /* These are firmware-submit boundaries, not host/PC receive latency. */
    uint16_t latestSampleToCh585SubmitUs;
    uint16_t avgSampleToCh585SubmitUs;
    uint16_t latestSampleToRfSubmitUs;
    uint16_t avgSampleToRfSubmitUs;
    uint16_t rfTransferOkLow16;
    uint16_t rfTransferFailLow16;
};

struct MonitorPowerFrameV1 {
    uint32_t magic; /* "MONP" */
    uint32_t seq;
    uint32_t timestampMs;
    uint16_t h1Mv;
    uint16_t h2Mv;
    uint16_t batMv;
    uint16_t socPermille;
    uint8_t activeBattery;
    uint8_t chargeState;
    uint8_t flags;
    uint8_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
};

/*
 * Latest-PCB power/CH585 status.  V1 remains available for existing monitor
 * clients; V2 uses a new magic so the two layouts cannot be confused.
 */
struct MonitorPowerFrameV2 {
    uint32_t magic; /* "MPW2" */
    uint32_t seq;
    uint32_t timestampMs;
    uint16_t cellMv;
    uint16_t socPermille;
    uint16_t vbusMv;
    uint16_t chargeCurrentMa;
    uint16_t faultBits;
    uint8_t chargeState;
    uint8_t ch585Role; /* 0 unknown, 1 RF, 2 USB, 3 maintenance */
    uint8_t ch585VersionMajor;
    uint8_t ch585VersionMinor;
    uint8_t ch585VersionPatch;
    uint8_t flags;
    uint32_t reserved;
};

static_assert(sizeof(MonitorTelemetryFrameV1) == 32, "MonitorTelemetryFrameV1 must stay 32 bytes");
static_assert(sizeof(MonitorPowerFrameV1) == 32, "MonitorPowerFrameV1 must stay 32 bytes");
static_assert(sizeof(MonitorPowerFrameV2) == 32, "MonitorPowerFrameV2 must stay 32 bytes");

void MonitorTelemetry_Init(ConnectionMode mode, uint16_t targetRateHz);
void MonitorTelemetry_SetTargetRateHz(uint16_t targetRateHz);
uint32_t MonitorTelemetry_NextSequence();
void MonitorTelemetry_OnReportReady(uint32_t seq,
                                    uint32_t triggerCycles,
                                    uint32_t completeCycles);
bool MonitorTelemetry_GetReportTriggerCycles(uint32_t seq,
                                             uint32_t* outTriggerCycles);
void MonitorTelemetry_SetPendingUsbSeq(uint32_t seq);
void MonitorTelemetry_OnUsbReportSubmitted(uint16_t reportLen);
void MonitorTelemetry_OnRfTransfer(uint32_t seq, uint8_t cmd, uint8_t payloadLen, bool ok);
void MonitorTelemetry_OnLinkStateChanged(ConnectionMode mode, uint8_t linkState);
void MonitorTelemetry_OnError(const char* source, uint32_t code, const char* message);
void MonitorTelemetry_SetCh585Status(uint8_t role, uint8_t versionMajor, uint8_t versionMinor, uint8_t versionPatch);
void MonitorTelemetry_GetSnapshot(MonitorTelemetrySnapshot* out);
bool MonitorTelemetry_FillFrameV1(MonitorTelemetryFrameV1* out);
bool MonitorTelemetry_FillPowerFrameV1(MonitorPowerFrameV1* out);
bool MonitorTelemetry_FillPowerFrameV2(MonitorPowerFrameV2* out);

#endif
