"""Offline v5 analysis. Reads JSONL only; never connects to any device."""
import argparse
import json
import math
from pathlib import Path


def percentile(values, q):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * q) - 1)] if values else None


def summarize(path, since=None, until=None):
    events, corrupt = [], 0
    with Path(path).open(encoding="utf-8-sig") as stream:
        for line in stream:
            try:
                event = json.loads(line)
            except (ValueError, TypeError):
                corrupt += 1
                continue
            at = event.get("timestampMs", 0)
            if (since is None or at >= since) and (until is None or at <= until):
                events.append(event)
    events.sort(key=lambda e: e.get("timestampMs", 0))
    windows = [e for e in events if e.get("messageType", "").startswith("RFH_RHM1_")]
    received = sum(e.get("sampleCount", 0) for e in windows)
    expected = sum(e.get("expectedCount", 0) for e in windows)
    duration = sum(e.get("sampleWindowMs", 0) for e in windows)
    snapshots = [e["rfTxMetrics"] for e in events if e.get("rfTxMetrics")]
    tx = {}; tx_us = 0; rejected = 0
    for a, b in zip(snapshots, snapshots[1:]):
        dt = (b["atUs"] - a["atUs"]) & 0xffffffff
        sequence = (b["snapshot"] - a["snapshot"]) & 65535
        delta = {k: (v - a["totals"][k]) & 0xffffffff for k, v in b["totals"].items() if k != "lateMaxUs"}
        if not 0 < sequence <= 15 or not 100000 <= dt <= 30000000 or any(
            v > (dt + 10000 if k == "ackUs" else math.ceil(dt * .02) + 100) for k, v in delta.items()
        ):
            rejected += 1
            continue
        tx_us += dt
        for k, v in delta.items():
            tx[k] = tx.get(k, 0) + v
    air = [e for e in events if "rfAirReceivedTotal" in e and "rfAirMissingTotal" in e]
    air_rx = air_missing = 0
    for a, b in zip(air, air[1:]):
        dt = b["timestampMs"] - a["timestampMs"]
        rx = (b["rfAirReceivedTotal"] - a["rfAirReceivedTotal"]) & 0xffffffff
        missing = (b["rfAirMissingTotal"] - a["rfAirMissingTotal"]) & 0xffffffff
        if 0 < dt <= 30000 and rx + missing <= dt * 20 + 100:
            air_rx += rx; air_missing += missing
    traces = {}
    for index, e in enumerate(events):
        if e.get("kind") == "button_latency" and e.get("measurement") == "usb":
            key = e.get("traceId") or f"unidentified:{index}"
            traces[key] = e
    complete = [e["latencyMs"] for e in traces.values() if e.get("latencyMs") is not None]
    # RX-local histogram windows, never host USB arrival times or lifetime maxima.
    pending, delivered = {}, set()
    bins = [0] * 8; gap_max = None; gap_us = 0; gap_windows = 0
    for e in events:
        if not e.get("messageType", "").startswith("RFH_RIG5_"):
            continue
        try:
            raw = bytes.fromhex(e["payloadHex"])
            if len(raw) != 32 or raw[7] != 1 or raw[6] >= 3:
                continue
            at = e["timestampMs"]
            for key in list(pending):
                if at - pending[key]["first"] > 10000:
                    del pending[key]
            key = (int.from_bytes(raw[4:6], "little"), int.from_bytes(raw[8:12], "little"), int.from_bytes(raw[12:16], "little"))
            if key in delivered:
                continue
            row = pending.setdefault(key, {"first": at, "pages": {}})
            row["pages"][raw[6]] = [int.from_bytes(raw[i:i+4], "little") for i in range(16, 32, 4)]
            if len(row["pages"]) == 3:
                values = sum((row["pages"][i] for i in range(3)), [])
                bins = [a + b for a, b in zip(bins, values[:8])]
                gap_max = max(gap_max or 0, values[8]); gap_us += key[2]; gap_windows += 1
                delivered.add(key); del pending[key]
        except (KeyError, ValueError):
            corrupt += 1
    def gap_percentile(q):
        count = sum(bins)
        if not count:
            return None
        total = 0
        for bound, n in zip([125, 250, 500, 1000, 2000, 4000, 8000, ">8000"], bins):
            total += n
            if total >= math.ceil(count * q):
                return bound
    return {
        "source": str(path), "events": len(events), "corruptLines": corrupt,
        "fromMs": events[0]["timestampMs"] if events else None, "toMs": events[-1]["timestampMs"] if events else None,
        "input": {"windows": len(windows), "elapsedMs": duration, "received": received, "expected": expected,
                  "rateHz": received * 1000 / duration if duration else None,
                  "deficitPercent": 100 * (expected-received) / expected if expected else None,
                  "nonConnectedWindows": sum(e.get("rfStateCode") != "C" for e in windows)},
        "air": {"received": air_rx, "missing": air_missing, "gapPercent": 100*air_missing/(air_missing+air_rx) if air_rx+air_missing else None},
        "tx": {"elapsedUs": tx_us, "deltaTotals": tx or None, "rejectedIntervals": rejected,
               "ackPercent": 100 * tx.get("ackUs", 0) / tx_us if tx_us else None,
               "timerMissedIsEstimate": True},
        "latencyMs": {"complete": len(complete), "partial": len(traces)-len(complete),
                      **{name: percentile(complete, q) for name, q in [("p50", .5), ("p95", .95), ("p99", .99), ("max", 1)]}},
        "inputGapUs": {"windows": gap_windows, "elapsedUs": gap_us, "samples": sum(bins), "histogram": bins,
                       "p50UpperBound": gap_percentile(.5), "p95UpperBound": gap_percentile(.95),
                       "p99UpperBound": gap_percentile(.99), "max": gap_max},
        "limitations": "No automatic pass verdict. Windows differ between sources; missing data is null. Gap quantiles are bin upper bounds between valid RX DATA in a session, not downtime across reconnects. Latency partial samples are counted, never completed with zeros.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log"); parser.add_argument("--baseline")
    parser.add_argument("--since-ms", type=int); parser.add_argument("--until-ms", type=int)
    parser.add_argument("--out")
    args = parser.parse_args()
    result = {"current": summarize(args.log, args.since_ms, args.until_ms)}
    if args.baseline:
        result["baseline"] = summarize(args.baseline)
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)


if __name__ == "__main__":
    main()
