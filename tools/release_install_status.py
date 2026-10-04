"""Read whole-release installation journals; never erase, flash or retry an update."""
from __future__ import annotations

import argparse
import ctypes
import json
import subprocess
import tempfile
import zlib
from pathlib import Path

from build import BuildTool
from ch585_stlink_update import _probe_uid
from webconfig_flash import resolve_openocd_executable

ROOT = Path(__file__).resolve().parents[1]
BANK_ADDRESS = 0x90574000
BANK_BYTES = 0x6000
PHASES = ('idle', 'receiving', 'prepared', 'activated', 'tx-writing', 'tx-verified',
          'committing', 'verifying', 'completed', 'failed', 'aborted', 'declared',
          'backing-up-tx', 'tx-restoring', 'restored', 'restore-failed', 'rollback-verifying')


class Snapshot(ctypes.LittleEndianStructure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ('magic', 'generation', 'phase', 'attempts', 'configVersion', 'manifestSize')] + [
        ('session', ctypes.c_char * 33), ('confirmedVersion', ctypes.c_char * 32),
        ('confirmedDigest', ctypes.c_char * 65), ('error', ctypes.c_char * 96),
        ('source', ctypes.c_ubyte * 807), ('target', ctypes.c_ubyte * 807),
        ('signature', ctypes.c_ubyte * 64), ('manifest', ctypes.c_char * 8193),
        ('crc', ctypes.c_uint32), ('commit', ctypes.c_uint32),
        ('restoreAttempts', ctypes.c_uint32), ('backupAddress', ctypes.c_uint32),
        ('backupSize', ctypes.c_uint32), ('backupReady', ctypes.c_uint32),
        ('oldTx', ctypes.c_ubyte * 121), ('oldStm', ctypes.c_ubyte * 121),
        ('backupSha', ctypes.c_char * 65), ('installError', ctypes.c_char * 96),
        ('recoveryError', ctypes.c_char * 96), ('errorCode', ctypes.c_char * 40),
        ('crc2', ctypes.c_uint32), ('commit2', ctypes.c_uint32),
        ('txInstallMode', ctypes.c_uint32), ('txRecoveryMode', ctypes.c_uint32),
        ('crc3', ctypes.c_uint32), ('commit3', ctypes.c_uint32),
    ]


def parse_journals(data: bytes) -> dict:
    if len(data) != BANK_BYTES * 2:
        raise ValueError('Expected exactly two 24 KiB release journal banks')
    candidates = []
    for index in range(2):
        raw = data[index * BANK_BYTES:(index + 1) * BANK_BYTES]
        state = Snapshot.from_buffer_copy(raw[:ctypes.sizeof(Snapshot)])
        legacy = state.magic == 0x32524F58
        no_extension = all(getattr(state, name) == 0xffffffff for name in ('txInstallMode', 'txRecoveryMode', 'crc3', 'commit3'))
        extension = not legacy and not no_extension
        crc_name, commit_name = ('crc', 'commit') if legacy else ('crc2', 'commit2')
        crc_offset = getattr(Snapshot, crc_name).offset
        if state.magic not in (0x32524F58, 0x33524F58) or not state.generation:
            continue
        if extension and (state.txInstallMode > 2 or state.txRecoveryMode > 2 or
                          state.commit3 != 0x54494D43 or state.crc3 != zlib.crc32(raw[:Snapshot.crc3.offset])):
            continue
        if state.phase > (10 if legacy else 16) or state.manifestSize > 8192:
            continue
        if getattr(state, commit_name) != 0x54494D43 or getattr(state, crc_name) != zlib.crc32(raw[:crc_offset]):
            continue
        if raw[Snapshot.manifest.offset + state.manifestSize] != 0:
            continue
        fields = ('session', 'confirmedVersion', 'confirmedDigest', 'error')
        if not legacy:
            fields += ('backupSha', 'installError', 'recoveryError', 'errorCode')
        if any(raw[getattr(Snapshot, name).offset + getattr(Snapshot, name).size - 1] != 0 for name in fields):
            continue
        decode = lambda name: bytes(getattr(state, name)).decode('utf-8', errors='replace')
        result = dict(bank=index, generation=state.generation, phase=PHASES[state.phase],
                      installAttempts=state.attempts, error=decode('error'), legacy=legacy)
        modes = ('unknown', 'small-packet', 'dma')
        result.update(txInstallMode=modes[state.txInstallMode] if extension else 'unknown',
                      txRecoveryMode=modes[state.txRecoveryMode] if extension else 'unknown')
        if legacy:
            result.update(errorCode='LEGACY_NO_BACKUP' if state.phase >= 3 and state.phase <= 9 else '',
                          restoreAttempts=0, backupReady=False)
        else:
            result.update(restoreAttempts=state.restoreAttempts, backupReady=bool(state.backupReady),
                          backupAddress=f'0x{state.backupAddress:08X}', backupSize=state.backupSize,
                          installError=decode('installError'), recoveryError=decode('recoveryError'),
                          errorCode=decode('errorCode'))
            identity = lambda field: {
                'version': bytes(getattr(state, field))[24:56].split(b'\0')[0].decode('ascii', errors='replace'),
                'buildId': bytes(getattr(state, field))[56:121].split(b'\0')[0].decode('ascii', errors='replace'),
            }
            result.update(sourceController=identity('oldStm'), sourceTx=identity('oldTx'))
        candidates.append(result)
    if not candidates:
        return {'phase': 'idle' if all(b == 0xFF for b in data) else 'invalid-journal'}
    latest = candidates[0]
    for candidate in candidates[1:]:
        delta = (candidate['generation'] - latest['generation']) & 0xFFFFFFFF
        if 0 < delta < 0x80000000:
            latest = candidate
    return latest


def read_device(openocd: Path, swd_khz: int) -> bytes:
    # Existing target binding reads only chip identity; no protection registers.
    uid = _probe_uid(openocd, swd_khz, wait_seconds=0.001)
    with tempfile.TemporaryDirectory(prefix='xora-release-status-') as temp:
        destination = Path(temp) / 'journal.bin'
        commands = ['gdb_port disabled', 'tcl_port disabled', 'telnet_port disabled',
                    'init', 'halt', 'qspi_init']
        commands += BuildTool._openocd_target_assert_commands(uid)
        commands += ['flash probe 1',
                     f'flash read_bank 1 {BuildTool._openocd_tcl_braced_path(destination, must_exist=False)} '
                     f'0x{BANK_ADDRESS - 0x90000000:X} 0x{BANK_BYTES * 2:X}',
                     'qspi_init', 'resume', 'shutdown']
        script = Path(temp) / 'read.tcl'
        script.write_text('\n'.join(commands) + '\n', encoding='utf-8')
        command = [str(openocd), '-d0', '-f', str(ROOT / 'application/Openocd_Script/ST-LINK-QSPIFLASH.cfg'),
                   '-c', f'adapter speed {swd_khz}', '-f', str(script)]
        result = subprocess.run(command, cwd=ROOT / 'application', capture_output=True,
                                text=True, timeout=30)
        if result.returncode:
            raise RuntimeError('Read-only QSPI journal access failed; device log was not modified')
        return destination.read_bytes()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--read-device', action='store_true')
    source.add_argument('--file', type=Path, help='parse a previously captured two-bank dump')
    parser.add_argument('--openocd', type=Path)
    parser.add_argument('--swd-khz', type=int, default=1800, choices=range(50, 10001))
    args = parser.parse_args()
    data = args.file.read_bytes() if args.file else read_device(
        resolve_openocd_executable(args.openocd, allow_automatic=True), args.swd_khz)
    print(json.dumps(parse_journals(data), ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
