"""Analyze exported records, using only same-device durations (no hardware I/O)."""
import argparse
import collections
import json
import math
from pathlib import Path

NAMES = {2: 'recovery_detection_to_input', 6: 'across_switch_input_gap',
         7: 'recovery_commit_elapsed', 15: 'injected_fault_start_to_input',
         16: 'logical_fault_clear_to_input'}

def distribution(values):
    values = sorted(values)
    def q(f):
        return values[max(0, math.ceil(f * len(values))-1)] if values else None
    return dict(samples=len(values), p50Us=q(.5), p95Us=q(.95), maxUs=q(1))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('jsonl', type=Path)
    ap.add_argument('--out', type=Path)
    args = ap.parse_args()
    groups = collections.defaultdict(list)
    faults = collections.Counter()
    overflows = {}
    commits = collections.Counter()
    maintenance = collections.defaultdict(list)
    seen = set()
    summary = None
    for line in args.jsonl.read_text(encoding='utf-8-sig').splitlines():
        row = json.loads(line)
        if row.get('type') == 'fast_test_summary':
            summary = row
        status = row.get('rfFast')
        if status:
            key = f"role{status['role']}/test{status['testId']}"
            overflows[key] = max(overflows.get(key, 0), status['overflow'])
        e = row.get('rfFastEvent')
        if not e:
            continue
        identity = (e['role'], e['testId'], e['sequence'], e['atUs'], e['event'])
        if identity in seen:
            continue
        seen.add(identity)
        key = f"role{e['role']}/test{e['testId']}"
        if e['event'] in NAMES:
            groups[(key, NAMES[e['event']])].append(e['value'])
        if e['event'] == 7:
            commits[key] += 1
        # Mode state is latched=10; fault injection event=8 is not in itself
        # a recovery failure. Do not count deliberately dropped ACKs as failures.
        if e['event'] == 1 and (e['value'] & 255) == 10:
            faults[key] += 1
        if e['event'] == 20:
            dt = (e['atUs'] - e['plannedUs']) & 0xffffffff
            if 0 < dt < 0x80000000:
                maintenance[key].append(100 * e['value'] / dt)
    report = {
        'hardwareAcceptance': False,
        'logIntegrity': 'known-overflow' if any(overflows.values()) or (summary or {}).get('truncated', False) else 'no-known-overflow' if seen else 'no-records',
        'note': 'role 0=RX, 1=TX. Separate local clocks; no cross-device subtraction. '
                'No samples means unmeasured. Maintenance is observation intervals, '
                'not a proof for every sliding 1s. Injection is logical, not real interference. '
                'Cross-switch samples include recovery retunes: use separate single-switch runs for the 1ms target.',
        'measurements': {f'{key}/{name}': distribution(v) for (key, name), v in groups.items()},
        'observedCommitSuccesses': dict(commits), 'observedLatchedFailures': dict(faults),
        'eventOverflow': overflows,
        'maintenancePercent': {k: {'intervals': len(v), 'max': max(v)} for k, v in maintenance.items()},
    }
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.out:
        args.out.write_text(text+'\n', encoding='utf-8')
    else:
        print(text)

if __name__ == '__main__':
    main()
