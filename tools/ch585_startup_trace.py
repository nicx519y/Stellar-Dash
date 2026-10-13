"""Decode an existing 512-byte g_ch585_startup RAM dump; never access hardware."""
import argparse
import json
from pathlib import Path
import struct

EVENTS = {
    1: "power_on", 2: "ready_wait_begin", 3: "line_sample",
    4: "ready_wait_end", 5: "first_send_begin", 6: "first_send_end",
    7: "first_read_begin", 8: "first_read_end", 9: "release_fault",
    10: "selection_end",
    11: "application_handoff_begin", 12: "application_handoff_end",
}


def decode(data):
    if len(data) != 512:
        raise ValueError("Expected exactly 512 bytes")
    h = struct.unpack_from("<12I", data)
    if h[:3] != (0x43535431, 1, 512):
        raise ValueError("Uninitialized or unsupported startup trace ABI")
    if h[3] != 2:
        raise ValueError("Trace is not sealed/cache-published; read after first selection ends")
    if h[5] > 24 or h[7] > 80 or h[11] > 2:
        raise ValueError("Invalid startup trace bounds")
    events = []
    for i in range(h[5]):
        tick, kind, arg0, arg1 = struct.unpack_from("<4I", data, 48 + i * 16)
        events.append(dict(tick_ms=tick, event=EVENTS.get(kind, f"unknown_{kind}"),
                           arg0=arg0, arg1=arg1))
    return dict(time_origin="Application HAL tick, not physical power-on",
                role=h[4], event_count=h[5], dropped_events=h[6],
                send_count=h[9], read_count=h[10], first_raw_result=h[8],
                first_raw_hex=data[432:432 + h[7]].hex(" "), events=events,
                interpretation={
                    "ready_wait_begin": "arg0=timeout budget ms",
                    "line_sample": "arg0=high; sampled observations, not exact electrical edges",
                    "ready_wait_end": "arg0=complete ready pulse observed",
                    "first_send_begin": "arg0=line high before local SPI transfer",
                    "first_send_end": "arg0=HAL result, arg1=line high after NSS release",
                    "first_read_end": "arg0=HAL result; arg1 low16=bytes, bit16=line high at read end",
                    "release_fault": "arg0=release wait start ms, arg1=deadline ms",
                    "selection_end": "arg0=selected on first power-on attempt",
                    "application_handoff_end": "arg0=HAL result, arg1=line high; separate from first role send",
                    "first_raw_result": "0=valid frame, 1=HAL failure, 2=invalid frame, 3=unexpected high-speed block, 4294967295=no read",
                    "event_order": "Append order; first_read_end can be appended after release_fault with its earlier captured timestamp",
                })


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(decode(args.dump.read_bytes()), indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(1, f"error: {exc}\n")


if __name__ == "__main__":
    main()
