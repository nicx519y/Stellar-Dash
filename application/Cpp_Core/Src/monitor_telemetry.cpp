#include "monitor_telemetry.hpp"

#include <string.h>

#include "micro_timer.hpp"
#include "system_logger.h"

namespace {
constexpr uint32_t SAMPLE_SLOT_COUNT = 128u;
constexpr uint32_t LOG_SAMPLE_INTERVAL = 200u;

struct ReportTimestampSlot {
    uint32_t seq;
    uint32_t reportReadyUs;
    bool valid;
};

ReportTimestampSlot g_slots[SAMPLE_SLOT_COUNT];
MonitorTelemetrySnapshot g_snapshot;
uint32_t g_sequence = 0u;
uint32_t g_pendingUsbSeq = 0u;
bool g_hasPendingUsbSeq = false;

inline void update_avg(uint32_t& avg, uint32_t latest) {
    if (avg == 0u) {
        avg = latest;
        return;
    }
    avg = (avg * 7u + latest) / 8u;
}

void remember_report(uint32_t seq, uint32_t t0_us) {
    const uint32_t idx = seq % SAMPLE_SLOT_COUNT;
    g_slots[idx].seq = seq;
    g_slots[idx].reportReadyUs = t0_us;
    g_slots[idx].valid = true;
}

bool query_report_ready_time(uint32_t seq, uint32_t* out_t0_us) {
    if (out_t0_us == nullptr) {
        return false;
    }

    const uint32_t idx = seq % SAMPLE_SLOT_COUNT;
    if (!g_slots[idx].valid || g_slots[idx].seq != seq) {
        return false;
    }

    *out_t0_us = g_slots[idx].reportReadyUs;
    return true;
}
}

void MonitorTelemetry_Init(ConnectionMode mode, uint16_t targetRateHz) {
    memset(g_slots, 0, sizeof(g_slots));
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_sequence = 0u;
    g_pendingUsbSeq = 0u;
    g_hasPendingUsbSeq = false;

    g_snapshot.connectionMode = static_cast<uint8_t>(mode);
    g_snapshot.targetRateHz = targetRateHz;
    g_snapshot.lastUpdateUs = MICROS_TIMER.micros();

    LOG_INFO("MON", "monitor telemetry init mode=%u targetRate=%u", g_snapshot.connectionMode, g_snapshot.targetRateHz);
}

uint32_t MonitorTelemetry_NextSequence() {
    g_sequence++;
    return g_sequence;
}

void MonitorTelemetry_OnReportReady(uint32_t seq) {
    const uint32_t now_us = MICROS_TIMER.micros();
    remember_report(seq, now_us);

    g_snapshot.totalReports++;
    g_snapshot.lastUpdateUs = now_us;

    if ((g_snapshot.totalReports % LOG_SAMPLE_INTERVAL) == 0u) {
        LOG_DEBUG("MON", "report sample seq=%lu total=%lu", seq, g_snapshot.totalReports);
    }
}

void MonitorTelemetry_SetPendingUsbSeq(uint32_t seq) {
    g_pendingUsbSeq = seq;
    g_hasPendingUsbSeq = true;
}

void MonitorTelemetry_OnUsbReportSubmitted(uint16_t reportLen) {
    const uint32_t now_us = MICROS_TIMER.micros();
    g_snapshot.usbReportsCompleted++;
    g_snapshot.lastUsbReportLen = reportLen;
    g_snapshot.lastUpdateUs = now_us;

    if (g_hasPendingUsbSeq) {
        uint32_t t0_us = 0u;
        if (query_report_ready_time(g_pendingUsbSeq, &t0_us)) {
            const uint32_t latency = now_us - t0_us;
            g_snapshot.latestUsbLatencyUs = latency;
            update_avg(g_snapshot.avgUsbLatencyUs, latency);
        }
        g_hasPendingUsbSeq = false;
    }
}

void MonitorTelemetry_OnRfTransfer(uint32_t seq, uint8_t cmd, uint8_t payloadLen, bool ok) {
    const uint32_t now_us = MICROS_TIMER.micros();
    g_snapshot.lastUpdateUs = now_us;
    g_snapshot.lastRfCmd = cmd;
    g_snapshot.lastRfPayloadLen = payloadLen;

    if (ok) {
        g_snapshot.rfTransferOk++;
    } else {
        g_snapshot.rfTransferFail++;
    }

    uint32_t t0_us = 0u;
    if (query_report_ready_time(seq, &t0_us)) {
        const uint32_t latency = now_us - t0_us;
        g_snapshot.latestRfLatencyUs = latency;
        update_avg(g_snapshot.avgRfLatencyUs, latency);
    }

    if (!ok) {
        LOG_WARN("MON", "rf transfer failed seq=%lu cmd=%u payloadLen=%u", seq, cmd, payloadLen);
    }
}

void MonitorTelemetry_OnLinkStateChanged(ConnectionMode mode, uint8_t linkState) {
    g_snapshot.connectionMode = static_cast<uint8_t>(mode);
    g_snapshot.linkState = linkState;
    g_snapshot.lastUpdateUs = MICROS_TIMER.micros();
    LOG_INFO("MON", "link state changed mode=%u state=%u", g_snapshot.connectionMode, g_snapshot.linkState);
}

void MonitorTelemetry_OnError(const char* source, uint32_t code, const char* message) {
    g_snapshot.errorCount++;
    g_snapshot.lastUpdateUs = MICROS_TIMER.micros();
    LOG_ERROR("MON", "err source=%s code=%lu msg=%s", source ? source : "unknown", code, message ? message : "");
}

void MonitorTelemetry_GetSnapshot(MonitorTelemetrySnapshot* out) {
    if (out == nullptr) {
        return;
    }
    *out = g_snapshot;
}

bool MonitorTelemetry_FillFrameV1(MonitorTelemetryFrameV1* out) {
    if (out == nullptr) {
        return false;
    }

    MonitorTelemetrySnapshot snapshot = {};
    MonitorTelemetry_GetSnapshot(&snapshot);

    out->magic = 0x4D4F4E31u;
    out->seq = snapshot.totalReports;
    out->totalReports = snapshot.totalReports;
    out->usbReportsCompleted = snapshot.usbReportsCompleted;
    out->targetRateHz = snapshot.targetRateHz;
    out->lastUsbReportLen = snapshot.lastUsbReportLen;
    out->latestUsbLatencyUs = (uint16_t)(snapshot.latestUsbLatencyUs & 0xFFFFu);
    out->avgUsbLatencyUs = (uint16_t)(snapshot.avgUsbLatencyUs & 0xFFFFu);
    out->latestRfLatencyUs = (uint16_t)(snapshot.latestRfLatencyUs & 0xFFFFu);
    out->avgRfLatencyUs = (uint16_t)(snapshot.avgRfLatencyUs & 0xFFFFu);
    out->rfTransferOkLow16 = (uint16_t)(snapshot.rfTransferOk & 0xFFFFu);
    out->rfTransferFailLow16 = (uint16_t)(snapshot.rfTransferFail & 0xFFFFu);
    return true;
}
