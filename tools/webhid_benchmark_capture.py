"""Read a completed XORA bandwidth snapshot via a minimal, non-halting Cortex-M target."""
from __future__ import annotations
import argparse, csv, hashlib, json, pathlib, re, struct, subprocess

FIELDS = ('magic version size consistency run_id state direction seed bytes reports crc32 errors '
          'started_ms elapsed_ms duration_ms spi_hz report_bytes window rx_peak decrypt_us '
          'process_us dma_us dma_blocks dma_errors tx_usb_speed tx_queue_peak tx_backpressure '
          'tx_spi_blocks tx_crc_errors tx_protocol_errors tx_duplicates connection_epoch').split()
SIZE = 160

def symbol(elf, nm):
    text = subprocess.check_output([nm, '-S', '--defined-only', str(elf)], text=True, timeout=20)
    matches = re.findall(r'^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s+g_webhid_benchmark$', text, re.M)
    if len(matches) != 1: raise ValueError('ELF must contain exactly one development benchmark symbol')
    address, size = (int(value, 16) for value in matches[0])
    # D3 SRAM only, excluding all boot profile/attestation reserved memory.
    if size != SIZE or not 0x38000000 <= address < address + size <= 0x3800F800:
        raise ValueError('benchmark symbol is outside its permitted SRAM region')
    return address

def tcl_path(path):
    value = pathlib.Path(path).resolve().as_posix()
    if any(char in value for char in '{}\r\n'): raise ValueError('unsupported Tcl path')
    return '{' + value + '}'

def capture_script(serial, address, output):
    if not re.fullmatch('[0-9a-fA-F]{24}', serial): raise ValueError('explicit ST-LINK serial required')
    if not 0x38000000 <= address < address + SIZE <= 0x3800F800: raise ValueError('SRAM boundary')
    return f'''adapter driver st-link
adapter serial {serial}
transport select dapdirect_swd
adapter speed 1000
source [find target/swj-dp.tcl]
swj_newdap bench cpu -irlen 4 -expected-id 0x6ba02477
dap create bench.dap -chain-position bench.cpu
target create bench.cpu cortex_m -dap bench.dap
tcl_port disabled
telnet_port disabled
gdb_port disabled
init
dump_image {tcl_path(output)} 0x{address:08x} {SIZE}
dump_image {tcl_path(str(output)+'.check')} 0x{address:08x} {SIZE}
shutdown
'''

def decode(raw):
    if len(raw) != SIZE: raise ValueError('truncated snapshot')
    data = dict(zip(FIELDS, struct.unpack_from('<32I', raw)))
    if data['magic'] != 0x32424858 or data['version'] != 2 or data['size'] != SIZE or data['consistency'] & 1:
        raise ValueError('uninitialized, incompatible or torn snapshot')
    if data['state'] not in (3, 4): raise ValueError('capture is allowed only after the run stops')
    data['build_id'] = raw[128:160].split(b'\0')[0].decode('ascii', errors='strict')
    return data

def sha(path): return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=pathlib.Path)
    parser.add_argument('--tx-image', required=True, type=pathlib.Path)
    parser.add_argument('--manifest', required=True, type=pathlib.Path)
    parser.add_argument('--browser-report', required=True, type=pathlib.Path)
    parser.add_argument('--run-id', required=True, type=int)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--nm', default='arm-none-eabi-nm')
    parser.add_argument('--openocd', default='openocd')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    if manifest.get('bootSecurityMode') != 'unlocked-development' or manifest.get('requiresManualLifecycleProvisioning'):
        raise ValueError('unlocked-development manifest required')
    browser = json.loads(args.browser_report.read_text(encoding='utf-8'))
    matches = [entry for entry in (browser if isinstance(browser, list) else [browser]) if entry.get('runId') == args.run_id]
    if len(matches) != 1: raise ValueError('exact browser run ID required')
    browser = matches[0]
    address = symbol(args.elf, args.nm)
    args.output.mkdir(parents=True, exist_ok=True)
    raw_path = args.output / f'{args.run_id}.bin'
    script_path = args.output / f'{args.run_id}.cfg'
    script_path.write_text(capture_script(args.serial, address, raw_path), encoding='utf-8')
    print(f'Reading completed run {args.run_id}, 2 x {SIZE} SRAM bytes; CPU stays running.', flush=True)
    with (args.output / f'{args.run_id}.log').open('w', encoding='utf-8') as log:
        subprocess.run([args.openocd, '-f', str(script_path.resolve())], stdout=log, stderr=subprocess.STDOUT, timeout=25, check=True)
    raw = raw_path.read_bytes()
    if raw != pathlib.Path(str(raw_path)+'.check').read_bytes(): raise ValueError('snapshot changed between reads')
    device = decode(raw)
    if device['run_id'] != args.run_id or device['bytes'] != browser['bytes'] or device['crc32'] != browser['crc32']:
        raise ValueError('browser/device run evidence mismatch')
    if (device['build_id'] != browser['device']['buildId'] or
        device['report_bytes'] != browser['reportBytes'] or
        device['window'] != browser['window'] or
        device['spi_hz'] != browser['device']['spiHz']):
        raise ValueError('browser/device build or configuration mismatch')
    verified = device['state'] == 3 and device['errors'] == 0
    result = dict(verified=verified, browser=browser, device=device, elf_sha256=sha(args.elf),
                  tx_sha256=sha(args.tx_image), manifest_sha256=sha(args.manifest), symbol_address=address)
    (args.output / f'{args.run_id}.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    row = dict(run_id=args.run_id, verified=verified, bytes=device['bytes'], crc32=device['crc32'],
               bytes_per_second=browser['bytesPerSecond'], spi_hz=device['spi_hz'], report_bytes=device['report_bytes'], window=device['window'])
    with (args.output / f'{args.run_id}.csv').open('w', newline='', encoding='utf-8') as output:
        writer=csv.DictWriter(output,fieldnames=row.keys()); writer.writeheader(); writer.writerow(row)
    report = (
        f"# XORA WebHID run {args.run_id}\n\n"
        f"Receiver verified: {verified}. Bytes: {device['bytes']}; CRC32: {device['crc32']:08x}.\n\n"
        f"Effective throughput: {browser['bytesPerSecond'] / 1e6:.3f} MB/s. "
        f"SPI: {device['spi_hz']} Hz; HID report: {device['report_bytes']} bytes; window: {device['window']}.\n\n"
        f"Browser: {browser.get('browserVersion', 'unknown')}. Web: {browser.get('webVersion', 'unknown')}.\n\n"
        f"Device build: {device['build_id']}. ELF SHA-256: {result['elf_sha256']}. "
        f"TX image SHA-256: {result['tx_sha256']}.\n\n"
        "Hashes identify the supplied artifacts; this SRAM capture is not firmware attestation. "
        "TX counters cover the current bridge connection, not just this run. "
        "Concurrent browser timing sums overlap and must not be added as wall time.\n"
    )
    (args.output / f'{args.run_id}.md').write_text(report, encoding='utf-8')
    print(json.dumps(row))
    return 0 if verified else 1

if __name__ == '__main__': raise SystemExit(main())
