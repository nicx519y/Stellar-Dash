#!/usr/bin/env python3
"""Diagnostic SRAM collector. No Flash driver, protection access, halt or breakpoint."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import math
import re
import statistics
import struct
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

ADDRESS, MAGIC, SIZE, CAPACITY = 0x3800F800, 0x31504248, 1024, 80
END, NO_TICK = 0x8000, 0xFFFFFFFF
HEADER = ("magic version count status initial_hz boot_hz app_hz reset_flags "
          "log_cycles log_calls overhead_min overhead_max overhead_samples "
          "flags domain sequence").split()
NAMES = {
    1: "baseline", 2: "safe_power", 3: "power_wait_20ms", 4: "system_clock",
    5: "peripheral_clock", 6: "usart", 7: "qspi_total", 8: "qspi_controller",
    9: "qspi_reset", 10: "qspi_id", 11: "qspi_quad", 12: "qspi_wait_50ms",
    13: "memory_map", 14: "metadata_total", 15: "unmap", 16: "unmap_wait_5ms",
    17: "metadata_read", 18: "metadata_structure_crc", 19: "slot_total",
    20: "metadata_signature", 21: "application_sha256", 22: "hash_compare",
    23: "vector_checks", 24: "attestation_total", 25: "identity_read",
    26: "public_key_derivation", 27: "certificate_verify", 28: "rng_init",
    29: "boot_nonce", 30: "boot_keygen", 31: "boot_signature", 32: "context_commit",
    33: "teardown", 34: "jump_pre_msp", 100: "application_entry",
    101: "bss_done", 102: "data_done", 103: "rodata_done", 104: "copies_done",
    105: "system_init_done", 106: "constructors_done", 107: "application_main",
    108: "application_hal_ready", 109: "application_board_begin",
    110: "application_clock_ready", 111: "application_board_done",
    112: "application_loop_entry",
    200: "app_staging_check", 201: "app_mode_sample", 202: "app_config_load",
    203: "app_recovery_power", 204: "app_screen_setup", 205: "app_screen_first_frame",
    206: "app_power_setup", 207: "app_state_enter",
    208: "app_ch585_start", 209: "app_ch585_ready_wait", 210: "app_ch585_role_select",
    211: "app_usb_prepare", 212: "app_usb_connect", 213: "app_input_pipeline",
    214: "app_connection_setup", 220: "app_state_input", 221: "app_state_web_config",
    222: "app_state_calibration", 223: "app_state_bridge_update", 224: "app_state_safe_recovery",
}


def elapsed(a, b, h, *, upper_bound_ms=None, clock_change=False):
    """Reject mixed clocks and intervals without a full-wrap guard."""
    ticked = a["tick"] != NO_TICK and b["tick"] != NO_TICK
    ticks = (b["tick"]-a["tick"]) & 0xFFFFFFFF if ticked else None
    if ticks is not None and ticks >= 0x80000000:
        return None, "invalid_tick_order"
    if clock_change:
        return (ticks, "HAL_tick_approx_1ms") if ticked else (None, "mixed_clock")
    if a["domain"] == b["domain"] == 2:
        return (ticks, "application_HAL_tick_approx_1ms") if ticked else (None, "missing_app_tick")
    frequencies = {0: h["initial_hz"], 1: h["boot_hz"],
                   3: h["boot_hz"], 4: h["app_hz"]}
    hz = frequencies.get(a["domain"], 0)
    if not hz or frequencies.get(b["domain"], 0) != hz:
        return None, "mixed_or_unknown_clock"
    if h["flags"] & 1:
        return (ticks, "DWT_unavailable_HAL_tick") if ticked else (None, "DWT_unavailable")
    bound = ticks+2 if ticked else upper_bound_ms
    if bound is None or bound >= 2**32*1000/hz:
        return (ticks, "DWT_wrap_unguarded_HAL_tick") if ticked else (None, "DWT_wrap_unguarded")
    ms = ((b["cycles"]-a["cycles"]) & 0xFFFFFFFF)*1000/hz
    if ticked and abs(ms-ticks) > 2:
        return None, "cycle_tick_disagreement"
    if not ticked and upper_bound_ms is not None and ms > upper_bound_ms:
        return None, "cycle_exceeds_capture_bound"
    return ms, "DWT_single_wrap" if b["cycles"] < a["cycles"] else "DWT"


def decode(data, *, upper_bound_ms=None):
    if len(data) != SIZE:
        raise ValueError("dump must contain exactly 1024 bytes")
    h = dict(zip(HEADER, struct.unpack_from("<16I", data)))
    if h["magic"] != MAGIC or h["version"] not in (1, 2, 3, 4):
        raise ValueError("missing/incompatible diagnostic image pair")
    if h["count"] > CAPACITY:
        raise ValueError("corrupt event count")
    events = []
    for i in range(h["count"]):
        tag, tick, cycles = struct.unpack_from("<3I", data, 64+i*12)
        ident, domain = tag & 0x7FFF, tag >> 16
        if ident not in NAMES or domain not in (0, 1, 2, 3, 4):
            raise ValueError("unknown event/domain")
        events.append(dict(id=ident, end=bool(tag & END), domain=domain, tick=tick, cycles=cycles))
    endpoint = 107 if h["version"] == 1 else 112
    app_marks = [e["id"] for e in events if 107 <= e["id"] <= 112]
    complete = (h["status"] == 1 and bool(events) and events[-1]["id"] == endpoint
                and (h["version"] == 1 or app_marks == list(range(107, 113))))
    stack, rows, warnings = [], [], []
    if not complete:
        warnings.append("overflow" if h["status"] == 2 else "incomplete_boot")
    if h["flags"] & 8:
        warnings.append("attestation_failed")
    if h["flags"] & 16:
        warnings.append("application_details_omitted_capacity")
    baseline = next((e for e in events if e["id"] == 1), None)
    teardown = next((e for e in events if e["id"] == 33), None)
    if not baseline or not teardown or not events or events[0]["id"] != 1:
        warnings.append("missing_baseline_or_teardown")
    total = ((teardown["tick"]-baseline["tick"]) & 0xFFFFFFFF
             if baseline and teardown else None)
    board_done = next((e for e in events if e["id"] == 111), None)
    loop_entry = next((e for e in events if e["id"] == 112), None)
    app_total = elapsed(board_done, loop_entry, h)[0] if board_done and loop_entry else None
    for index, event in enumerate(events):
        ident = event["id"]
        app_detail = 200 <= ident <= 224
        if not (2 <= ident <= 32 or app_detail):
            continue
        if not event["end"]:
            stack.append((index, event, []))
            continue
        if not stack or stack[-1][1]["id"] != ident:
            warnings.append("unmatched_stage_end")
            continue
        start_index, start, children = stack.pop()
        ms, quality = elapsed(start, event, h, upper_bound_ms=upper_bound_ms, clock_change=ident == 4)
        if ms is None:
            warnings.append("invalid_boot_timing:"+NAMES[ident]+":"+quality)
        exclusive = (ms-sum(children) if ms is not None
                     and all(x is not None for x in children) else None)
        if exclusive is not None and exclusive < -0.01:
            exclusive = None
            warnings.append("negative_exclusive_time")
        rows.append(dict(stage=NAMES[ident], start_index=start_index,
            inclusive_ms=ms, exclusive_ms=exclusive, quality=quality,
            end_from_baseline_ms=((event["tick"]-baseline["tick"]) & 0xFFFFFFFF)
                if not app_detail and baseline and event["tick"] != NO_TICK else None,
            boot_share_percent=100*exclusive/total if not app_detail and exclusive is not None and total else None,
            app_init_share_percent=100*exclusive/app_total
                if app_detail and exclusive is not None and app_total else None))
        if stack:
            stack[-1][2].append(ms)
    if stack:
        warnings.append("unfinished_stage")
    marks = [e for e in events if e["id"] in (33, 34, *range(100, 113))]
    for a, b in zip(marks, marks[1:]):
        if a["id"] == 109 and b["id"] == 110:
            # HAL reconfigures SysTick while changing clocks; its ticks are
            # not a reliable wall-time reference for this transition.
            ms, quality = None, "application_clock_transition_unmeasured"
        else:
            ms, quality = elapsed(a, b, h, upper_bound_ms=upper_bound_ms, clock_change=b["id"] == 105)
        exclusive = ms
        if a["id"] == 111 and b["id"] == 112:
            children = [r["exclusive_ms"] for r in rows if r["stage"].startswith("app_")]
            exclusive = ms-sum(children) if ms is not None and all(x is not None for x in children) else None
            if exclusive is not None and exclusive < 0:
                warnings.append("negative_app_exclusive_time")
                exclusive = None
        rows.append(dict(stage=NAMES[a["id"]]+"_to_"+NAMES[b["id"]],
            inclusive_ms=ms, exclusive_ms=exclusive, quality=quality,
            end_from_baseline_ms=None, boot_share_percent=None))
    log_ms = h["log_cycles"]*1000/h["boot_hz"] if h["boot_hz"] and not h["flags"] & 3 else None
    # Reaching main does not prove development-mode attestation succeeded.
    required = {"metadata_signature", "application_sha256", "certificate_verify",
                "boot_keygen", "boot_signature", "context_commit"}
    missing = sorted(required-{r["stage"] for r in rows})
    if missing:
        warnings.append("missing_normal_path:"+",".join(missing))
    return dict(header=h, complete=complete, baseline_eligible=complete and not warnings,
        warnings=warnings, events=events, rows=rows, boot_to_teardown_ms=total, log_ms=log_ms,
        overhead_min_us=h["overhead_min"]*1e6/h["boot_hz"] if h["boot_hz"] else None,
        overhead_max_us=h["overhead_max"]*1e6/h["boot_hz"] if h["boot_hz"] else None,
        uncovered=["power_to_baseline", "application_SystemInit_wall_time"] +
            (["application_main_to_HAL_ready", "application_board_clock_switch"] if h["version"] >= 2 else []),
        note="Do not sum inclusive parents with children. Log aggregate is already part of total.")


def tcl_path(path):
    value = Path(path).resolve().as_posix()
    if any(c in value for c in '{}\n\r'):
        raise ValueError("unsupported Tcl path")
    return "{"+value+"}"


def capture_script(serial, output, *, reset, timeout_ms=15000):
    if not re.fullmatch(r"[0-9A-Fa-f]{24}", serial):
        raise ValueError("explicit 24-hex ST-LINK serial required")
    # Minimal Cortex-M target: no STM32 Flash bank, reset-init/examine hooks.
    return f"""adapter driver st-link
adapter serial {serial}
transport select dapdirect_swd
adapter speed 1000
source [find target/swj-dp.tcl]
swj_newdap bp cpu -irlen 4 -expected-id 0x6ba02477
dap create bp.dap -chain-position bp.cpu
target create bp.cpu cortex_m -dap bp.dap
tcl_port disabled
telnet_port disabled
gdb_port disabled
init
set old [read_memory 0x3800F83C 32 1]
{'mww 0xE000ED0C 0x05FA0004' if reset else '# manual cold boot; no reset request'}
set ready 0
for {{set i 0}} {{$i < {timeout_ms//50}}} {{incr i}} {{
    after 50
    if {{[catch {{read_memory 0x3800F800 32 16}} h]}} {{continue}}
    if {{[lindex $h 0] == {MAGIC} && [lindex $h 1] >= 1 && [lindex $h 1] <= 4 && [lindex $h 3] != 0}} {{
        if {{{1 if reset else 0} && [lindex $h 15] == [lindex $old 0]}} {{continue}}
        set ready 1
        break
    }}
}}
dump_image {tcl_path(output)} 0x3800F800 1024
if {{!$ready}} {{echo "BOOT_PROFILE_INCOMPLETE_OR_STALE"; shutdown error}}
after 50
dump_image {tcl_path(str(output)+'.check')} 0x3800F800 1024
shutdown
"""


def run_capture(args):
    if args.kind == "reset" and args.power_cycled:
        raise ValueError("--power-cycled cannot label a software-reset sample")
    if args.kind == "cold" and not args.power_cycled:
        raise ValueError("cold sample requires --power-cycled after a real manual power cycle")
    if args.kind == "cold" and args.count != 1:
        raise ValueError("one confirmed physical power cycle per cold sample")
    if not 1 <= args.count <= 30:
        raise ValueError("count must be 1..30")
    manifest = Path(args.manifest)
    mode = json.loads(manifest.read_text(encoding="utf-8"))
    if mode.get("bootSecurityMode") != "unlocked-development" or mode.get("requiresManualLifecycleProvisioning"):
        raise ValueError("requires verified unlocked-development artifacts")
    slot = mode.get("targetSlot")
    if slot not in ("A", "B"):
        raise ValueError("manifest must identify Slot A or B")
    for name in ("bootloader.bin", f"application-slot-{slot.lower()}.bin", "metadata.bin"):
        entry = mode.get("files", {}).get(name, {})
        contents = (manifest.parent / name).read_bytes()
        if len(contents) != entry.get("bytes") or hashlib.sha256(contents).hexdigest() != entry.get("sha256"):
            raise ValueError("artifact hash/length mismatch: "+name)
    folder = Path(args.output)
    folder.mkdir(parents=True, exist_ok=True)
    for index in range(args.count):
        stem = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        dump = folder/(stem+".bin")
        script = folder/(stem+".cfg")
        script.write_text(capture_script(args.serial, dump, reset=args.kind == "reset"), encoding="utf-8")
        command = [args.openocd, "-f", str(script.resolve())]
        metadata = dict(kind=args.kind, phase=args.phase, timestamp=stem, serial=args.serial,
            manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
            command=command, physical_power_cycle_confirmed=args.power_cycled)
        start = time.monotonic()
        print(f"START {args.phase}/{args.kind} {index+1}/{args.count}", flush=True)
        with (folder/(stem+".log")).open("w", encoding="utf-8") as log:
            try:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=25)
                metadata["returncode"] = result.returncode
            except subprocess.TimeoutExpired:
                metadata["error"] = "capture_timeout"
        metadata["capture_elapsed_ms"] = (time.monotonic()-start)*1000
        try:
            if not dump.exists():
                raise ValueError("no SRAM dump; inspect OpenOCD log")
            if metadata.get("returncode") != 0:
                raise ValueError("collector failed; partial/stale dump retained and excluded")
            if dump.read_bytes() != Path(str(dump)+'.check').read_bytes():
                raise ValueError("SRAM changed between reads")
            # Only a reset requested inside this process has a host wall-time bound.
            upper = metadata["capture_elapsed_ms"] if args.kind == "reset" else None
            metadata["profile"] = decode(dump.read_bytes(), upper_bound_ms=upper)
        except (ValueError, OSError) as exc:
            metadata["error"] = str(exc)
        (folder/(stem+".json")).write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        print("END", "FAILED" if "error" in metadata else "captured", flush=True)
        if "error" in metadata or not metadata["profile"]["baseline_eligible"]:
            raise ValueError("sample not a normal complete boot; evidence retained; batch stopped")


def summarize(folder):
    samples, failures, groups = [], [], {}
    for path in sorted(Path(folder).glob('*.json')):
        record = json.loads(path.read_text(encoding="utf-8"))
        if "kind" not in record:
            continue
        if "error" in record or not record.get("profile", {}).get("baseline_eligible"):
            failures.append(dict(file=path.name, error=record.get("error", record.get("profile", {}).get("warnings"))))
            continue
        profile = record["profile"]
        samples.append(dict(file=path.name, kind=record["kind"], phase=record["phase"], **profile))
        key = (record["phase"], record["kind"], record["manifest_sha256"])
        group = groups.setdefault(key, {})
        per_boot = {}
        for row in profile["rows"]:
            for metric in ("inclusive_ms", "exclusive_ms"):
                ms = row.get(metric)
                if ms is not None:
                    stage_key = (row["stage"], metric)
                    per_boot[stage_key] = per_boot.get(stage_key, 0)+ms
        for name, value in [("boot_to_teardown_total", profile["boot_to_teardown_ms"]),
                            ("startup_log_aggregate", profile["log_ms"])]:
            if value is not None:
                per_boot[(name, "aggregate_ms")] = value
        for name, value in per_boot.items():
            group.setdefault(name, []).append(value)
    summary = []
    for key, stages in groups.items():
        for name, values in stages.items():
            ordered = sorted(values)
            summary.append(dict(phase=key[0], kind=key[1], manifest_sha256=key[2], stage=name[0], metric=name[1],
                n=len(values), median_ms=statistics.median(values),
                p95_ms=ordered[math.ceil(.95*len(values))-1], max_ms=max(values),
                insufficient_samples=len(values)<(30 if key[1]=="reset" else 10)))
    return dict(samples=samples, summary=summary, failures=failures,
        limitations=["No measured power-on total.", "Missing times are not zero.",
                     "Cold app cycles lack a full-wrap guard; retain raw counters.",
                     "Overhead is measured at stable boot clock only; no automatic subtraction."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    capture = sub.add_parser("capture")
    for name in ("openocd", "serial", "manifest", "output"):
        capture.add_argument("--"+name, required=True)
    capture.add_argument("--phase", choices=["smoke", "baseline"], required=True)
    capture.add_argument("--kind", choices=["reset", "cold"], required=True)
    capture.add_argument("--count", type=int, default=1)
    capture.add_argument("--power-cycled", action="store_true")
    report = sub.add_parser("report")
    report.add_argument("folder")
    args = parser.parse_args()
    try:
        if args.operation == "capture":
            run_capture(args)
        else:
            result = summarize(args.folder)
            folder = Path(args.folder)
            (folder/"report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
            rows = result["summary"]
            with (folder/"summary.csv").open("w", newline="", encoding="utf-8-sig") as output:
                writer = csv.DictWriter(output, fieldnames=list(rows[0]) if rows else ["stage", "n"])
                writer.writeheader()
                writer.writerows(rows)
            print(f"{len(result['samples'])} valid samples, {len(result['failures'])} failed samples")
    except (ValueError, OSError) as exc:
        parser.exit(1, str(exc)+"\n")


if __name__ == "__main__":
    main()
