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

    uint32_t latestUsbLatencyUs;
    uint32_t latestRfLatencyUs;
    uint32_t avgUsbLatencyUs;
    uint32_t avgRfLatencyUs;

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
    uint16_t latestUsbLatencyUs;
    uint16_t avgUsbLatencyUs;
    uint16_t latestRfLatencyUs;
    uint16_t avgRfLatencyUs;
    uint16_t rfTransferOkLow16;
    uint16_t rfTransferFailLow16;
};

void MonitorTelemetry_Init(ConnectionMode mode, uint16_t targetRateHz);
uint32_t MonitorTelemetry_NextSequence();
void MonitorTelemetry_OnReportReady(uint32_t seq);
void MonitorTelemetry_SetPendingUsbSeq(uint32_t seq);
void MonitorTelemetry_OnUsbReportSubmitted(uint16_t reportLen);
void MonitorTelemetry_OnRfTransfer(uint32_t seq, uint8_t cmd, uint8_t payloadLen, bool ok);
void MonitorTelemetry_OnLinkStateChanged(ConnectionMode mode, uint8_t linkState);
void MonitorTelemetry_OnError(const char* source, uint32_t code, const char* message);
void MonitorTelemetry_GetSnapshot(MonitorTelemetrySnapshot* out);
bool MonitorTelemetry_FillFrameV1(MonitorTelemetryFrameV1* out);

#endif
