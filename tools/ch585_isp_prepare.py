"""Prepare a reviewed 4 KiB TX IAP package for manual WCHISPStudio use.

Offline only: never launches an ISP utility, discovers a target or writes a chip.
Package preparation does not approve V3.9 erase/configuration behavior.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

if __package__:
    from . import ch585_iap_maintenance as maintenance
else:
    import ch585_iap_maintenance as maintenance

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / 'RF_PHY_Hop/TX/build_tx'
OUTPUT = ROOT / '.hbox/tx-isp-new-board'
GUIDE = ROOT / 'docs/new-pcb-firmware-flash.md'


def intel_hex(image: bytes) -> str:
    if len(image) != maintenance.IAP_BYTES:
        raise ValueError('Only the independent 4096-byte TX IAP can be exported')
    lines = []
    for address in range(0, len(image), 16):
        body = image[address:address + 16]
        record = bytes((len(body), address >> 8, address & 255, 0)) + body
        lines.append(':' + (record + bytes(((-sum(record)) & 255,))).hex().upper())
    return '\n'.join(lines + [':00000001FF']) + '\n'


def build_iap(folder: Path, make: str) -> None:
    command = [make, '-C', 'RF_PHY_Hop/TX', '-B',
               'build_tx/RF_PHY_Hop_TX_iap_padded.bin']
    log = folder / 'build.log'
    started = time.monotonic()
    with log.open('w', encoding='utf-8') as output:
        process = subprocess.Popen(command, cwd=ROOT, stdout=output,
                                   stderr=subprocess.STDOUT,
                                   start_new_session=(os.name != 'nt'))
        print(f'IAP build: PID={process.pid} timeout=600s log={log}', flush=True)
        try:
            code = process.wait(timeout=600)
        except subprocess.TimeoutExpired:
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                               check=False, timeout=15)
            else:
                import signal
                os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=15)
            raise RuntimeError(f'IAP build timed out; retained log: {log}')
    print(f'IAP build: exit={code} elapsed={time.monotonic() - started:.1f}s', flush=True)
    if code:
        raise RuntimeError(f'IAP build failed; retained log: {log}')


def export_package(folder: Path, image_path: Path, elf_path: Path,
                   manifest_path: Path) -> dict:
    # Validate the complete original review before publishing any loadable file.
    image, elf = image_path.read_bytes(), elf_path.read_bytes()
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    maintenance.validate_iap(image, elf)
    maintenance.validate_manifest(manifest, image, elf)
    if image[0x14:0x18] != struct.pack('<I', 0xF3F9BDA9):
        raise ValueError('TX IAP vendor startup marker differs; export refused')
    guide = GUIDE.read_text(encoding='utf-8')
    hex_text = intel_hex(image)
    plan = {
        'schemaVersion': 1, 'product': 'XORA', 'target': 'CH585-TX-IAP',
        'intendedHostTool': 'WCHISPStudio V3.9',
        'address': 0, 'size': len(image), 'endAddressInclusive': 4095,
        'bootSecurityMode': 'unlocked-development',
        'requiresManualLifecycleProvisioning': False,
        'hardwareWrite': False, 'targetVerified': False,
        'downloadApproved': False,
        'approvalBlockers': [
            'V3.9 configuration-word writes and erase extent are not verified',
            'Physical TX target and blank-chip status are not verified',
            'Readback and startup acceptance are pending',
        ],
        'binFile': 'XORA_TX_IAP_4KiB.bin',
        'hexFile': 'XORA_TX_IAP_4KiB.hex',
        'sha256': maintenance.sha(image), 'elfSha256': maintenance.sha(elf),
        'hexSha256': maintenance.sha(hex_text.encode('ascii')),
        'sourceSha256': manifest['sourceSha256'],
        'scope': 'Only TX IAP 0x0000..0x0FFF; preserve Application, DataFlash and configuration/protection state',
    }
    if any(folder.glob('XORA_TX_IAP_4KiB.*')) or (folder / 'plan.json').exists():
        raise ValueError('Output package already exists; choose a new directory')
    folder.mkdir(parents=True, exist_ok=True)
    (folder / plan['binFile']).write_bytes(image)
    (folder / plan['hexFile']).write_text(hex_text, encoding='ascii', newline='\n')
    (folder / 'XORA_TX_IAP_4KiB.elf').write_bytes(elf)
    (folder / 'reviewed-manifest.json').write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (folder / 'plan.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
    (folder / 'README.md').write_text(guide, encoding='utf-8')
    return plan


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true', help='Rebuild only the independent TX IAP before export')
    parser.add_argument('--make', default=shutil.which('make') or 'make')
    parser.add_argument('--image', type=Path, default=ARTIFACTS / 'RF_PHY_Hop_TX_iap_padded.bin')
    parser.add_argument('--elf', type=Path, default=ARTIFACTS / 'RF_PHY_Hop_TX_iap.elf')
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    try:
        if args.build and (args.image != ARTIFACTS / 'RF_PHY_Hop_TX_iap_padded.bin' or
                           args.elf != ARTIFACTS / 'RF_PHY_Hop_TX_iap.elf' or args.manifest):
            raise ValueError('--build cannot be combined with alternate artifacts')
        folder = args.output_dir or OUTPUT / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        if folder.exists():
            raise ValueError('Output directory already exists; choose a new directory')
        if args.build:
            folder.mkdir(parents=True)
            build_iap(folder, args.make)
        plan = export_package(folder, args.image, args.elf,
                              args.manifest or args.image.with_suffix('.manifest.json'))
        print(f'Offline package: {folder.resolve()}')
        print(f'TX IAP: 4096 bytes at 0x0000..0x0FFF; SHA-256 {plan["sha256"]}')
        print('BIN/ELF/source review and HEX export passed. No device was accessed.')
        print('WCHISPStudio V3.9 download remains unapproved: verify erase/configuration behavior first.')
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'ISP preparation failed: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
