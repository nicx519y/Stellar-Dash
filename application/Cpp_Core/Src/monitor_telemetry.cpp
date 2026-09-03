#include "monitor_telemetry.hpp"

#include <string.h>

#include "board_cfg.h"
#include "cycle_elapsed.hpp"
#include "micro_timer.hpp"
#include "power_manager.hpp"
#include "stm32h7xx_hal.h"
#include "system_logger.h"

namespace {
constexpr uint32_t SAMPLE_SLOT_COUNT = 128u;
constexpr uint32_t LOG_SAMPLE_INTERVAL = 200u;
constexpr uint8_t POWER_FLAG_VALID = 0x01u;
constexpr uint8_t POWER_FLAG_LOW_BATTERY = 0x02u;
constexpr uint8_t POWER_FLAG_FAST_CHARGING = 0x04u;
constexpr uint8_t POWER_FLAG_VBUS_PRESENT = 0x08u;
constexpr uint8_t POWER_FLAG_GAUGE_ONLINE = 0x10u;
constexpr uint8_t POWER_FLAG_CHARGER_ONLINE = 0x20u;
constexpr uint32_t CYCLES_PER_US = SYSTEM_CLOCK_FREQ / 1000000u;

struct ReportTimestampSlot {
    uint32_t seq;
    uint32_t triggerCycles;
    uint32_t completeCycles;
    uint32_t reportReadyCycles;
    bool valid;
};

/*
 * This high-rate diagnostic history is explicitly cleared by
 * MonitorTelemetry_Init(), so it does not depend on startup .bss clearing.
 * Keep it in the existing RAM_D2 buffer area instead of consuming AXI RAM,
 * where the application also executes its text and read-only data.
 */
__attribute__((section(".DMA_Section"), aligned(32)))
ReportTimestampSlot g_slots[SAMPLE_SLOT_COUNT];
MonitorTelemetrySnapshot g_snapshot;
uint32_t g_sequence = 0u;
uint32_t g_pendingUsbSeq = 0u;
bool g_hasPendingUsbSeq = false;
uint8_t g_ch585Role = 0u;
uint8_t g_ch585VersionMajor = 0u;
uint8_t g_ch585VersionMinor = 0u;
uint8_t g_ch585VersionPatch = 0u;

inline void update_avg(uint32_t& avg, uint32_t latest) {
    if (avg == 0u) {
        avg = latest;
        return;
    }
    avg = (avg * 7u + latest) / 8u;
}

void remember_report(uint32_t seq,
                     uint32_t triggerCycles,
                     uint32_t completeCycles,
                     uint32_t reportReadyCycles) {
    const uint32_t idx = seq % SAMPLE_SLOT_COUNT;
    g_slots[idx].seq = seq;
    g_slots[idx].triggerCycles = triggerCycles;
    g_slots[idx].completeCycles = completeCycles;
    g_slots[idx].reportReadyCycles = reportReadyCycles;
    g_slots[idx].valid = true;
}

bool query_report_trigger_cycles(uint32_t seq, uint32_t* outCycles) {
    if (outCycles == nullptr) {
        return false;
    }

    const uint32_t idx = seq % SAMPLE_SLOT_COUNT;
    if (!g_slots[idx].valid || g_slots[idx].seq != seq) {
        return false;
    }

    *outCycles = g_slots[idx].triggerCycles;
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

void MonitorTelemetry_SetTargetRateHz(uint16_t targetRateHz) {
    g_snapshot.targetRateHz = targetRateHz;
    g_snapshot.lastUpdateUs = MICROS_TIMER.micros();
}

uint32_t MonitorTelemetry_NextSequence() {
    g_sequence++;
    return g_sequence;
}

void MonitorTelemetry_OnReportReady(uint32_t seq,
                                    uint32_t triggerCycles,
                                    uint32_t completeCycles) {
    const uint32_t readyCycles = MICROS_TIMER.cycles();
    remember_report(seq, triggerCycles, completeCycles, readyCycles);

    g_snapshot.totalReports++;
    g_snapshot.lastUpdateUs = MICROS_TIMER.micros();
    g_snapshot.latestAdcConversionUs =
        ElapsedMicros32(triggerCycles, completeCycles, CYCLES_PER_US);
    g_snapshot.latestInputProcessingUs =
        ElapsedMicros32(completeCycles, readyCycles, CYCLES_PER_US);

    if ((g_snapshot.totalReports % LOG_SAMPLE_INTERVAL) == 0u) {
        LOG_DEBUG("MON", "report sample seq=%lu total=%lu", seq, g_snapshot.totalReports);
    }
}

bool MonitorTelemetry_GetReportTriggerCycles(uint32_t seq,
                                             uint32_t* outTriggerCycles) {
    return query_report_trigger_cycles(seq, outTriggerCycles);
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
        const uint32_t idx = g_pendingUsbSeq % SAMPLE_SLOT_COUNT;
        if (g_slots[idx].valid && g_slots[idx].seq == g_pendingUsbSeq) {
            const uint32_t nowCycles = MICROS_TIMER.cycles();
            g_snapshot.latestAdcConversionUs = ElapsedMicros32(
                g_slots[idx].triggerCycles,
                g_slots[idx].completeCycles,
                CYCLES_PER_US);
            g_snapshot.latestInputProcessingUs = ElapsedMicros32(
                g_slots[idx].completeCycles,
                g_slots[idx].reportReadyCycles,
                CYCLES_PER_US);
            g_snapshot.latestReportSubmitUs = ElapsedMicros32(
                g_slots[idx].reportReadyCycles,
                nowCycles,
                CYCLES_PER_US);
            const uint32_t latency =
                g_snapshot.latestAdcConversionUs +
                g_snapshot.latestInputProcessingUs +
                g_snapshot.latestReportSubmitUs;
            g_snapshot.latestSampleToCh585SubmitUs = latency;
            update_avg(g_snapshot.avgSampleToCh585SubmitUs, latency);
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

    uint32_t triggerCycles = 0u;
    if (query_report_trigger_cycles(seq, &triggerCycles)) {
        const uint32_t latency = MICROS_TIMER.elapsedMicros(triggerCycles);
        g_snapshot.latestSampleToRfSubmitUs = latency;
        update_avg(g_snapshot.avgSampleToRfSubmitUs, latency);
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

void MonitorTelemetry_SetCh585Status(uint8_t role, uint8_t versionMajor, uint8_t versionMinor, uint8_t versionPatch) {
    g_ch585Role = role;
    g_ch585VersionMajor = versionMajor;
    g_ch585VersionMinor = versionMinor;
    g_ch585VersionPatch = versionPatch;
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
    out->latestSampleToCh585SubmitUs = (uint16_t)(
        snapshot.latestSampleToCh585SubmitUs & 0xFFFFu);
    out->avgSampleToCh585SubmitUs = (uint16_t)(
        snapshot.avgSampleToCh585SubmitUs & 0xFFFFu);
    out->latestSampleToRfSubmitUs = (uint16_t)(
        snapshot.latestSampleToRfSubmitUs & 0xFFFFu);
    out->avgSampleToRfSubmitUs = (uint16_t)(
        snapshot.avgSampleToRfSubmitUs & 0xFFFFu);
    out->rfTransferOkLow16 = (uint16_t)(snapshot.rfTransferOk & 0xFFFFu);
    out->rfTransferFailLow16 = (uint16_t)(snapshot.rfTransferFail & 0xFFFFu);
    return true;
}

bool MonitorTelemetry_FillPowerFrameV1(MonitorPowerFrameV1* out) {
    if (out == nullptr) {
        return false;
    }

    MonitorTelemetrySnapshot snapshot = {};
    MonitorTelemetry_GetSnapshot(&snapshot);

    const PowerBatteryVoltages voltages = POWER_MANAGER.getVoltages();
    const float soc = POWER_MANAGER.getTotalSocPercent();
    const bool valid = POWER_MANAGER.isVoltageValid();
    uint8_t flags = 0u;
    if (valid) {
        flags |= POWER_FLAG_VALID;
    }
    if (POWER_MANAGER.isLowBattery()) {
        flags |= POWER_FLAG_LOW_BATTERY;
    }
    if (POWER_MANAGER.isFastCharging()) {
        flags |= POWER_FLAG_FAST_CHARGING;
    }

    out->magic = 0x4D4F4E50u;
    out->seq = snapshot.totalReports;
    out->timestampMs = HAL_GetTick();
    out->h1Mv = (uint16_t)(voltages.h1_mv & 0xFFFFu);
    out->h2Mv = (uint16_t)(voltages.h2_mv & 0xFFFFu);
    out->batMv = (uint16_t)(voltages.bat_mv & 0xFFFFu);
    out->socPermille = (uint16_t)((soc <= 0.0f) ? 0u : ((soc >= 100.0f) ? 1000u : (uint16_t)(soc * 10.0f + 0.5f)));
    out->activeBattery = static_cast<uint8_t>(POWER_MANAGER.getActiveDischargeBattery());
    out->chargeState = static_cast<uint8_t>(POWER_MANAGER.getChargeState());
    out->flags = flags;
    out->reserved0 = 0u;
    out->reserved1 = 0u;
    out->reserved2 = 0u;
    return true;
}

bool MonitorTelemetry_FillPowerFrameV2(MonitorPowerFrameV2* out) {
    if (out == nullptr) {
        return false;
    }

    MonitorTelemetrySnapshot snapshot = {};
    MonitorTelemetry_GetSnapshot(&snapshot);
    const PowerSnapshot power = POWER_MANAGER.getSnapshot();

    uint8_t flags = 0u;
    if (power.valid) {
        flags |= POWER_FLAG_VALID;
    }
    if (POWER_MANAGER.isLowBattery()) {
        flags |= POWER_FLAG_LOW_BATTERY;
    }
    if (power.fast_charge) {
        flags |= POWER_FLAG_FAST_CHARGING;
    }
    if (power.vbus_present) {
        flags |= POWER_FLAG_VBUS_PRESENT;
    }
    if (power.gauge_online) {
        flags |= POWER_FLAG_GAUGE_ONLINE;
    }
    if (power.charger_online) {
        flags |= POWER_FLAG_CHARGER_ONLINE;
    }

    out->magic = 0x3257504Du;
    out->seq = snapshot.totalReports;
    out->timestampMs = HAL_GetTick();
    out->cellMv = power.cell_mv;
    out->socPermille = power.soc_permille;
    out->vbusMv = power.vbus_mv;
    out->chargeCurrentMa = power.charge_current_ma;
    out->faultBits = power.fault_bits;
    out->chargeState = static_cast<uint8_t>(power.charge_state);
    out->ch585Role = g_ch585Role;
    out->ch585VersionMajor = g_ch585VersionMajor;
    out->ch585VersionMinor = g_ch585VersionMinor;
    out->ch585VersionPatch = g_ch585VersionPatch;
    out->flags = flags;
    out->reserved = 0u;
    return true;
}
