"""Summarize v2 measurement coverage and packet costs from an existing monitor log."""
import argparse
from collections import Counter
import json
import math
from rf_link_report import read_events


def distribution(values):
    values = sorted(values)
    return {"count": len(values), "p50": values[math.ceil(len(values)*.5)-1] if values else None,
            "p95": values[math.ceil(len(values)*.95)-1] if values else None,
            "max": max(values) if values else None}


def summarize(events):
    rows, counters = {}, {}
    for event in events:
        if event.get("kind") == "button_latency" and event.get("measurement") == "usb":
            if event.get("traceId"):
                rows[event["traceId"]] = event  # partial updates are not extra button presses
        if event.get("kind") == "packet":
            for key, value in event.items():
                if key.startswith(("rfTx", "rfAck", "rfControl", "rfTrace", "rfAir", "rfCrc", "airPending", "rfInputEdge")):
                    counters[key] = value
    complete = [r for r in rows.values() if isinstance(r.get("latencyMs"), (int, float))]
    names = ["adc", "logic", "spi_wait", "spi", "tx", "rf_model", "rx", "usb"]
    return {"endpoint": "sampling to USB IN completion; RF/IRQ boundary estimated",
            "events": len(rows), "complete": len(complete),
            "complete_fraction": len(complete)/len(rows) if rows else None,
            "incomplete_reasons": dict(Counter(r.get("measurementReason", "unknown") for r in rows.values() if r.get("latencyMs") is None)),
            "total_ms": distribution([r["latencyMs"] for r in complete]),
            "stages_us": {name: distribution([r["relativeStagesUs"][i] for r in rows.values()
                          if len(r.get("relativeStagesUs", [])) > i and r["relativeStagesUs"][i] is not None]) for i, name in enumerate(names)},
            "latest_counters": counters}


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log");parser.add_argument("--since-ms", type=int, default=0)
    args=parser.parse_args()
    print(json.dumps(summarize(read_events(args.log,args.since_ms)),ensure_ascii=False,indent=2))
