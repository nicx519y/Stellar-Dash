"""Independent WCH-Link TX IAP maintenance. Default is an offline plan.

Never invokes a flash driver, chip erase, options or protection commands.
Actual IAP writes are made by an audited RAM helper using the existing ISP585
range APIs; Application is backed up and compared after writing. Not an OTA path.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import secrets
import shutil
import struct
import subprocess
import sys
import time
import zlib
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / 'tools/ch585_iap_maintenance'
OUTPUT = ROOT / '.hbox/tx-iap-maintenance'
SDK = Path('E:/Works/CH585EVT/EVT/EXAM')
CONTROL = 0x20018000
IMAGE = 0x20019000
BACKUP = 0x2001A000
IAP_BYTES = 4096
APP_BYTES = 0x6F000
MAINTENANCE_MAGIC = 0x50414958
INITIALIZE_MAGIC = 0x4E494158


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def elf_loads(data: bytes):
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected a little-endian ELF32 image')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if h[1] != 2 or h[2] != 243 or h[9] != 32 or not h[10]:
        raise ValueError('Expected an executable RISC-V ELF')
    if h[5] > len(data) or h[10] * h[9] > len(data) - h[5]:
        raise ValueError('Truncated ELF program headers')
    loads = []
    for index in range(h[10]):
        kind, offset, vaddr, paddr, size, memsize, flags, _ = struct.unpack_from(
            '<IIIIIIII', data, h[5] + index * h[9])
        if kind != 1:
            continue
        if size > memsize or offset > len(data) or size > len(data) - offset:
            raise ValueError('Truncated ELF segment')
        loads.append((vaddr, paddr, data[offset:offset + size], memsize, flags))
    if not loads:
        raise ValueError('ELF has no load segments')
    return h[4], loads


def validate_iap(image: bytes, elf: bytes) -> None:
    if len(image) != IAP_BYTES:
        raise ValueError('IAP image must be exactly 4096 bytes; combined TX images are rejected')
    entry, loads = elf_loads(elf)
    if entry != 0:
        raise ValueError('IAP entry must be zero')
    reconstructed = bytearray(b'\xff' * IAP_BYTES)
    for _, paddr, body, _, _ in loads:
        if not body:
            continue
        if paddr >= IAP_BYTES or len(body) > IAP_BYTES - paddr:
            raise ValueError('IAP ELF includes bytes outside the IAP region')
        reconstructed[paddr:paddr + len(body)] = body
    if reconstructed != image:
        raise ValueError('IAP BIN does not match the ELF load image')


def validate_helper(elf: bytes) -> None:
    entry, loads = elf_loads(elf)
    if entry != 0x20000000:
        raise ValueError('Maintenance helper must enter at the dedicated RAM base')
    for vaddr, paddr, _, memsize, _ in loads:
        if (vaddr != paddr or vaddr < 0x20000000 or vaddr >= 0x20010000 or
                memsize > 0x20010000 - vaddr):
            raise ValueError('Maintenance helper has a load segment outside dedicated RAM')


def validate_manifest(manifest: dict, image: bytes, elf: bytes) -> None:
    if (manifest.get('target') != 'CH585-TX-IAP' or manifest.get('address') != 0 or
            manifest.get('size') != IAP_BYTES or
            manifest.get('bootSecurityMode') != 'unlocked-development' or
            manifest.get('requiresManualLifecycleProvisioning') is not False):
        raise ValueError('Manifest must declare independent 4 KiB unlocked TX IAP maintenance')
    if manifest.get('sha256') != sha(image) or manifest.get('elfSha256') != sha(elf):
        raise ValueError('IAP artifacts do not match the reviewed maintenance manifest')
    if manifest.get('iapTransfer') != {'version': 2, 'frameBytes': 1024, 'payloadBytes': 1000, 'spiHz': 7500000}:
        raise ValueError('Manifest does not declare the expected DMA IAP capability')
    expected = {'RF_PHY_Hop/TX/IAP/iap_main.c', 'RF_PHY_Hop/TX/Makefile', 'common/ch585_iap_protocol.h'}
    sources = manifest.get('sourceSha256', {})
    if set(sources) != expected or any(sha((ROOT / name).read_bytes()) != sources[name] for name in expected):
        raise ValueError('IAP source differs from the reviewed manifest; rebuild and review the IAP artifact')


def target_identity(application: bytes) -> dict:
    magic = b'XORAFW2\0'
    identities = []
    start = 0
    while True:
        offset = application.find(magic, start)
        if offset < 0:
            break
        start = offset + 1
        raw = application[offset:offset + 121]
        if len(raw) != 121:
            continue
        component, protocol, maintenance, config = struct.unpack_from('<IIII', raw, 8)
        if (component, protocol, maintenance, config) != (2, 2, 2, 0):
            continue
        version, build = raw[24:56].split(b'\0')[0], raw[56:121].split(b'\0')[0]
        if b'\0' not in raw[24:56] or b'\0' not in raw[56:121] or not version or not build:
            continue
        identities.append({'component': 'TX', 'version': version.decode('ascii'),
                           'buildId': build.decode('ascii')})
    if len(identities) != 1:
        raise ValueError('Connected Application must contain one protocol-2 TX identity; RX is rejected')
    return identities[0]


def checked_target(iap: bytes, application: bytes, image: bytes, *, initialize: bool) -> dict:
    if len(iap) != IAP_BYTES or len(application) != APP_BYTES:
        raise ValueError('Incomplete device readback')
    if not initialize:
        return target_identity(application)
    # Blank silicon has no firmware identity; the explicit CLI option designates TX.
    # Never use this mode to bypass identity or marker checks on populated devices.
    if application != b'\xff' * APP_BYTES or iap not in (b'\xff' * IAP_BYTES, image):
        raise ValueError('New TX initialization requires blank Application and blank IAP (or this exact already-installed IAP)')
    if image[0x14:0x18] != struct.pack('<I', 0xF3F9BDA9):
        raise ValueError('New TX IAP must contain the unmodified vendor startup marker')
    return {'component': 'TX', 'version': 'not-installed', 'buildId': 'blank',
            'roleIdentification': 'explicit --initialize-new-tx; blank silicon has no firmware identity'}


def tcl_path(path: Path) -> str:
    text = str(path.resolve()).replace('\\', '/')
    if any(c in text for c in '{}\n\r'):
        raise ValueError('Unsupported path characters')
    return '{' + text + '}'


def target_config() -> str:
    # Deliberately do not create a flash bank: all loads/dumps are memory ops.
    return '\n'.join([
        'gdb_port disabled', 'tcl_port disabled', 'telnet_port disabled',
        'adapter driver wlinke', 'adapter speed 6000', 'transport select sdi',
        'sdi newtap wch_riscv cpu -irlen 5 -expected-id 0x00001',
        'target create wch_riscv.cpu.0 wch_riscv -chain-position wch_riscv.cpu',
        'init', 'halt', 'mem2array chip 8 0x40001041 1',
        'if {$chip(0) != 0x85} {resume; error "Target is not CH585"}',
    ]) + '\n'


def require_wch_link() -> None:
    if sys.platform != 'win32':
        raise ValueError('This maintenance entry currently supports Windows WCH-Link only')
    command = ("@(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match "
               "'^USB\\\\VID_1A86&PID_8010\\\\' }).Count")
    result = subprocess.run(['powershell', '-NoProfile', '-Command', command],
                            capture_output=True, text=True, timeout=15, check=True)
    if result.stdout.strip() != '1':
        raise ValueError('Connect exactly one WCH-Link in RISC-V mode to TX; ST-LINK cannot perform this operation')


def openocd(script: Path, executable: Path, *, writing: bool) -> None:
    log = script.with_suffix('.log')
    print(f'[WCH-Link] {"IAP maintenance" if writing else "read-only inspection"}; log: {log}', flush=True)
    # During a write, the target helper completes or restores before EBREAK.
    # wait_halt has its own 30s deadline. Do not kill an active writer process.
    with log.open('w', encoding='utf-8') as output:
        process = subprocess.Popen([str(executable), '-d1', '-f', str(script)], stdout=output, stderr=subprocess.STDOUT)
        print(f'[WCH-Link] PID={process.pid}', flush=True)
        try:
            code = process.wait(timeout=None if writing else 30)
        except subprocess.TimeoutExpired:
            process.terminate(); process.wait(timeout=10)
            raise RuntimeError(f'Read-only inspection timed out; see {log}')
    if code:
        raise RuntimeError(f'WCH-Link phase failed ({code}); see {log}. No unlock or erase-all fallback will be attempted')


def resolve_openocd(path: Path | None) -> Path:
    if path:
        return path.resolve(strict=True)
    candidate = Path('D:/MounRiverStudio2/MounRiver_Studio2/resources/app/resources/win32/components/WCH/OpenOCD/OpenOCD/bin/openocd.exe')
    if candidate.is_file():
        return candidate
    raise ValueError('Pass --openocd pointing to the WCH OpenOCD executable')


def build_helper(folder: Path, sdk: Path) -> Path:
    compiler = shutil.which('riscv32-wch-elf-gcc')
    if not compiler:
        raise ValueError('riscv32-wch-elf-gcc is required in PATH')
    driver = sdk / 'SRC/StdPeriphDriver'
    output = folder / 'iap-maintenance-ram.elf'
    command = [compiler, '-march=rv32imcbxw', '-mabi=ilp32', '-Os', '-ffreestanding',
               '-fno-builtin', '-fno-delete-null-pointer-checks', '-msmall-data-limit=0',
               '-nostdlib', '-nostartfiles', '-ffunction-sections', '-fdata-sections',
               '-I' + str(driver / 'inc'), str(HELPER / 'ram.c'), str(HELPER / 'start.S'),
               '-T' + str(HELPER / 'ram.ld'), str(driver / 'libISP585.a'), '-lgcc',
               '-Wl,--gc-sections', '-Wl,-Map=' + str(folder / 'iap-maintenance-ram.map'),
               '-o', str(output)]
    started = time.monotonic()
    process = subprocess.Popen(command)
    print(f'[RAM helper] compile PID={process.pid} timeout=120s', flush=True)
    try:
        code = process.wait(timeout=120)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                       capture_output=True, timeout=15, check=False)
        process.wait(timeout=15)
        raise
    print(f'[RAM helper] exit={code} elapsed={time.monotonic() - started:.1f}s', flush=True)
    if code:
        raise subprocess.CalledProcessError(code, command)
    validate_helper(output.read_bytes())
    return output


def inspect_device(folder: Path, executable: Path, *, initialize: bool = False) -> tuple[bytes, bytes]:
    old = folder / 'before-iap.bin'; app = folder / 'before-application.bin'
    script = folder / 'inspect.tcl'
    # A blank chip has no executable Application; leave it halted for initialization.
    resume = '' if initialize else 'resume\n'
    script.write_text(target_config() +
        f'set failed [catch {{dump_image {tcl_path(old)} 0 0x1000\n'
        f'dump_image {tcl_path(app)} 0x1000 0x6F000}} reason]\n'
        + resume + 'if {$failed} {error $reason}\nshutdown\n', encoding='utf-8')
    openocd(script, executable, writing=False)
    old_bytes, app_bytes = old.read_bytes(), app.read_bytes()
    if len(old_bytes) != IAP_BYTES or len(app_bytes) != APP_BYTES:
        raise ValueError('Incomplete device readback')
    return old_bytes, app_bytes


def execute_script(folder: Path, helper: Path) -> Path:
    script = folder / 'install.tcl'
    commands = target_config()
    # Ordinary reset stops Application peripherals before replacing its RAM.
    # No flash bank is registered and no reset-init/program event is attached.
    commands += 'reset halt\n'
    for name, address in [('iap-maintenance-ram.elf', None), ('target-iap.bin', IMAGE),
                          ('before-iap.bin', BACKUP), ('control.bin', CONTROL)]:
        source = helper if address is None else folder / name
        suffix = '' if address is None else f' 0x{address:X} bin'
        commands += f'load_image {tcl_path(source)}{suffix}\nverify_image {tcl_path(source)}{suffix}\n'
    commands += ('resume 0x20000000\n'
                 'if {[catch {wait_halt 30000} reason]} {error "Maintenance outcome unknown; keep WCH-Link connected and inspect before reset"}\n'
                 f'dump_image {tcl_path(folder / "result.bin")} 0x{CONTROL:X} 40\n'
                 f'dump_image {tcl_path(folder / "after-iap.bin")} 0 0x1000\n'
                 f'dump_image {tcl_path(folder / "after-application.bin")} 0x1000 0x6F000\n'
                 'shutdown\n')
    script.write_text(commands, encoding='utf-8')
    return script


def validate_result(control: bytes, original: bytes, result: bytes,
                    app_before: bytes, app_after: bytes, expected: bytes) -> dict:
    if len(control) != 40:
        raise ValueError('Missing complete maintenance result; outcome unknown')
    fields = struct.unpack('<10I', control)
    status = {'state': fields[6], 'error': fields[7], 'restored': fields[8],
              'applicationUnchanged': app_before == app_after,
              'iapMatchesTarget': result == expected, 'iapMatchesOriginal': result == original}
    return status


def bound_result(raw: bytes, request: bytes) -> None:
    if len(raw) != 40 or len(request) != 40 or raw[:24] != request[:24] or raw[36:] != request[36:]:
        raise ValueError('Maintenance result does not match this request; keep target halted for diagnosis')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, default=ROOT / 'RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX_iap_padded.bin')
    parser.add_argument('--elf', type=Path, default=ROOT / 'RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX_iap.elf')
    parser.add_argument('--manifest', type=Path, help='Defaults to the IAP BIN sidecar .manifest.json')
    parser.add_argument('--sdk-root', type=Path, default=SDK)
    parser.add_argument('--openocd', type=Path)
    parser.add_argument('--initialize-new-tx', action='store_true',
                        help='Explicitly designate blank CH585 as TX; reject populated Application/IAP')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--inspect', action='store_true')
    group.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    try:
        image, elf = args.image.read_bytes(), args.elf.read_bytes()
        validate_iap(image, elf)
        manifest_path = args.manifest or args.image.with_suffix('.manifest.json')
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        validate_manifest(manifest, image, elf)
        print(f'TX IAP: 4096 bytes at 0x0000..0x0FFF; SHA-256 {sha(image)}')
        print('Scope: IAP only; no flash-driver erase, no configuration/protection operations')
        if not args.inspect and not args.execute:
            print('Offline plan checked. Use --inspect for target checks, --execute for independent maintenance.')
            return 0
        require_wch_link()
        executable = resolve_openocd(args.openocd)
        folder = OUTPUT / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        folder.mkdir(parents=True, exist_ok=False)
        (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        original, application = inspect_device(folder, executable, initialize=args.initialize_new_tx)
        identity = checked_target(original, application, image, initialize=args.initialize_new_tx)
        report = {'target': identity, 'oldIapSha256': sha(original), 'applicationSha256': sha(application),
                  'targetIapSha256': sha(image), 'backupDirectory': str(folder), 'hardwareWrite': False,
                  'initializeNewTx': args.initialize_new_tx}
        (folder / 'inspection.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(f'Target: CH585 TX {identity["version"]}; backup: {folder}')
        print(f'Current IAP matches target: {original == image}')
        if args.inspect or original == image:
            return 0
        if not args.initialize_new_tx and image[0x14:0x18] != original[0x14:0x18]:
            raise ValueError('Vendor startup marker differs; maintenance refused without any write')
        helper = build_helper(folder, args.sdk_root)
        (folder / 'target-iap.bin').write_bytes(image)
        nonce = secrets.randbits(32) or 1
        control = struct.pack('<10I', INITIALIZE_MAGIC if args.initialize_new_tx else MAINTENANCE_MAGIC,
                              1, IAP_BYTES, zlib.crc32(image),
                              zlib.crc32(original), zlib.crc32(application), 0, 0, 0, nonce)
        (folder / 'control.bin').write_bytes(control)
        print('Keep TX powered; WCH-Link remains connected throughout IAP maintenance.', flush=True)
        script = execute_script(folder, helper)
        openocd(script, executable, writing=True)
        raw = (folder / 'result.bin').read_bytes()
        bound_result(raw, control)
        result = validate_result(raw, original, (folder / 'after-iap.bin').read_bytes(),
                                 application, (folder / 'after-application.bin').read_bytes(), image)
        report.update(result); report['hardwareWrite'] = True
        (folder / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        if result['state'] != 2 or result['error'] or not result['applicationUnchanged'] or not result['iapMatchesTarget']:
            raise RuntimeError(f'Maintenance failed; result: {result}. Keep WCH-Link connected; no automatic reset')
        print('IAP installed and readback verified; Application unchanged.')
        print('未修改任何保护位或锁定状态')
        if args.initialize_new_tx:
            print('New TX IAP initialized; Application remains blank. Power-cycle, then install TX Application through the normal IAP route.')
        else:
            print('Power-cycle the device normally, then verify DMA negotiation during the next WebConfig upgrade.')
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'IAP maintenance failed: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
