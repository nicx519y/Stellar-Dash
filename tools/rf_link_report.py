"""Summarize RF monitor JSONL without opening/competing for the HID interface."""
import argparse
import json
import math


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        return {"samples": 0, "p50_ms": None, "p95_ms": None, "max_ms": None}
    return {"samples": len(ordered),
            "p50_ms": ordered[math.ceil(len(ordered) * .5) - 1],
            "p95_ms": ordered[math.ceil(len(ordered) * .95) - 1],
            "max_ms": ordered[-1]}


def summarize(events):
    initial, reconnect, host_recover, hops = [], [], [], []
    last_count = 0
    last_diag_seq = None
    lost_at = None
    previous_connected = False
    missed_samples = 0
    latest = {}
    losses = 0
    first_time = last_time = None
    for event in events:
        if event.get("kind") != "packet" or event.get("channel") != "RF":
            continue
        timestamp = event.get("timestampMs", 0)
        if first_time is None:
            first_time = timestamp
        last_time = timestamp
        message = event.get("messageType", "")
        if message == "RFH_RHD1_0":
            seq = event.get("seq", 0)
            if last_diag_seq is not None and seq < last_diag_seq:
                last_count = 0  # RX reboot: start a new sample epoch.
            last_diag_seq = seq
            count = event.get("rfConnectCount", 0)
            if count > last_count:
                missed_samples += max(0, count - last_count - 1)
                value = event.get("rfConnectMs")
                if value is not None:
                    (initial if count == 1 else reconnect).append(value)
            last_count = count
        if message.startswith("RFH_RHD1_"):
            for key in ("rfBuildId", "rfReadyMs", "usbReadyMs", "rfAckWatchdog", "rfAckLate",
                        "rfAckDuplicates", "rfInputEdgeDrop", "rfCrcTotal",
                        "airPendingDrop", "airPendingMax", "rfTxDiagnosticValid",
                        "rfTxWindowMs", "rfTxDue", "rfTxStarted", "rfTxDropped",
                        "rfTxDiagnosticAgeMs", "rfAirMissingTotal", "rfAirReceivedTotal",
                        "rfRxArmFailures", "rfRxRearmMaxUs", "rfRxCallbackMaxUs",
                        "rfInputCommitMaxUs", "rfInputCaptureMaxUs", "rfAckSendFailures",
                        "rfShortDecodedTotal"):
                if key in event:
                    latest[key] = event[key]
        if message.startswith("RFH_RHM1_"):
            connected = event.get("rfStateCode") in ("C", "HR")
            if previous_connected and not connected and lost_at is None:
                lost_at = timestamp
                losses += 1
            if connected and lost_at is not None:
                host_recover.append(timestamp - lost_at)
                lost_at = None
            previous_connected = connected
            if event.get("hopEvent") == "finish":
                duration = event.get("hopDurationMs", event.get("hopEventValue"))
                if duration is not None:
                    hops.append(duration)
    return {"observed_duration_ms": 0 if first_time is None else last_time - first_time,
            "initial_connect_rf_ms": distribution(initial),
            "reconnect_rf_ms": distribution(reconnect),
            "host_observed_recovery_ms": distribution(host_recover),
            "hop_handshake_ms": distribution(hops),
            "link_losses": losses, "unrecovered_at_log_end": lost_at is not None,
            "missing_connect_samples": missed_samples, "latest_counters": latest,
            "notes": ["RF connect times start at receiver discovery/recovery, not power-on or obstruction removal.",
                      "RF/USB ready timestamps use the SDK clock epoch.",
                      "Hop handshake duration is not input interruption duration.",
                      "Missing RHD1 data yields null percentiles, never a passing result."]}


def read_events(path, since_ms):
    with open(path, encoding="utf-8-sig") as stream:
        for line in stream:
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue  # A live writer can leave an incomplete last line.
            if isinstance(event, dict) and event.get("timestampMs", 0) >= since_ms:
                yield event


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", help="connect-monitor monitor-events.jsonl")
    parser.add_argument("--since-ms", type=int, default=0)
    args = parser.parse_args()
    print(json.dumps(summarize(read_events(args.log, args.since_ms)), indent=2, ensure_ascii=False))
