"""Keep TX powered in ROM ISP while STM32 is halted. No firmware writes.

Default: print the offline plan. --execute requires ST-LINK on the STM32.
Run before WCHISPStudio downloads, never during an active download.
After maintenance, disconnect the TX boot strap and power-cycle normally.
"""
from __future__ import annotations
import argparse
from datetime import datetime
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
GPIO_E = 0x58021000
GPIO_I = 0x58022000
RCC_AHB4ENR = 0x580244E0
DBGMCU = 0x5C001000
MAIN = 1 << 4
TX = 1 << 10
HOST = 1 << 13
NSS = 1 << 11
SPI_ANALOG_PINS = (5, 12, 14)


def mode_mask(pins):
    return sum(3 << (pin * 2) for pin in pins)


def modify(address, clear=0, set_bits=0):
    return f'mww 0x{address:08X} [expr {{([mrw 0x{address:08X}] & 0x{(~clear & 0xffffffff):08X}) | 0x{set_bits:08X}}}]'


def tcl_path(path):
    value = str(Path(path).resolve()).replace('\\', '/')
    if any(c in value for c in '{}\n\r'):
        raise ValueError('Unsupported path characters')
    return '{' + value + '}'


def validate_board():
    cfg = (ROOT / 'application/Inc/system/board_cfg.h').read_text(encoding='utf-8')
    expected = {'MAIN_POWER_EN_PORT':'GPIOI','MAIN_POWER_EN_PIN':'GPIO_PIN_4',
                'CH585_EN_PORT':'GPIOI','CH585_EN_PIN':'GPIO_PIN_10',
                'USB_HOST_EN_PORT':'GPIOI','USB_HOST_EN_PIN':'GPIO_PIN_13',
                'CH585_SPI_GPIO_PORT':'GPIOE','CH585_SPI_MISO_PIN':'GPIO_PIN_5',
                'CH585_SPI_NSS_PIN':'GPIO_PIN_11','CH585_SPI_SCK_PIN':'GPIO_PIN_12',
                'CH585_SPI_MOSI_PIN':'GPIO_PIN_14'}
    for name,value in expected.items():
        if not re.search(r'^#define\s+'+name+r'\s+'+value+r'\s*$',cfg,re.M):
            raise ValueError(f'Board pin map changed: {name}; maintenance refused')


def hold_script(expected_uid=None):
    if expected_uid is not None and not re.fullmatch('[0-9A-Fa-f]{24}',expected_uid):
        raise ValueError('--target-uid must contain exactly 24 hex characters')
    pins = (4,10,13)
    masks = mode_mask(pins)
    output_modes = sum(1 << (p*2) for p in pins)
    spi_masks = mode_mask(SPI_ANALOG_PINS)
    lines = [
        f'source {tcl_path(ROOT / "bootloader/Openocd_Script/stlink.cfg")}',
        'transport select hla_swd', 'adapter speed 400',
        'gdb_port disabled','tcl_port disabled','telnet_port disabled',
        'source [find target/swj-dp.tcl]',
        # Minimal Cortex-M7 target; no Flash banks or reset event scripts.
        'swj_newdap stm32h750 cpu -irlen 4 -ircapture 0x1 -irmask 0xf -expected-id 0x6ba02477',
        'dap create stm32h750.dap -chain-position stm32h750.cpu',
        'target create stm32h750.cpu0 cortex_m -endian little -dap stm32h750.dap -ap-num 0',
        'init','halt','wait_halt 3000',
        'if {([mrw 0x5C001000] & 0xFFF) != 0x450} {error "Not the qualified STM32H750 target"}',
        'set tx_isp_uid {}',
    ]
    for i in range(3):
        lines += [f'set tx_isp_word_{i} [mrw 0x{0x1ff1e800+i*4:08X}]',
                  f'append tx_isp_uid [format %08X $tx_isp_word_{i}]']
    lines += ['if {$tx_isp_uid == "000000000000000000000000" || $tx_isp_uid == "FFFFFFFFFFFFFFFFFFFFFFFF"} {error "Invalid STM32 UID"}']
    if expected_uid:
        lines += [f'if {{$tx_isp_uid != "{expected_uid.upper()}"}} {{error "STM32 UID mismatch"}}']
    lines += [
        'echo "STM32_UID=$tx_isp_uid"',
        # Debug watchdog freezes are volatile and only effective while halted.
        modify(DBGMCU+4,set_bits=0x00600000),
        modify(DBGMCU+0x34,set_bits=0x40), modify(DBGMCU+0x54,set_bits=0x40000),
        modify(RCC_AHB4ENR,set_bits=(1<<4)|(1<<8)),
        f'mdw 0x{RCC_AHB4ENR:08X} 1',
        # Detach SCK/MOSI/MISO from alternate functions; hold NSS inactive.
        f'mww 0x{GPIO_E+0x18:08X} 0x{NSS:08X}',
        modify(GPIO_E, clear=spi_masks | (3<<22), set_bits=spi_masks | (1<<22)),
        modify(GPIO_E+4,clear=NSS),
        modify(GPIO_E+0x0c,clear=spi_masks | (3<<22)),
        # Main/XIP power stays on, USB Host VBUS stays off; cold-start only TX.
        f'mww 0x{GPIO_I+0x18:08X} 0x{MAIN | ((TX|HOST)<<16):08X}',
        modify(GPIO_I,clear=masks,set_bits=output_modes),
        modify(GPIO_I+4,clear=MAIN|TX|HOST),
        modify(GPIO_I+0xc,clear=masks),
        'sleep 50',
        f'mww 0x{GPIO_I+0x18:08X} 0x{TX:08X}',
        f'if {{([mrw 0x{GPIO_I+0x14:08X}] & 0x{MAIN|TX|HOST:08X}) != 0x{MAIN|TX:08X}}} {{error "TX power latch verification failed"}}',
        f'if {{([mrw 0x{GPIO_I:08X}] & 0x{masks:08X}) != 0x{output_modes:08X}}} {{error "TX output-mode verification failed"}}',
        f'if {{([mrw 0x{GPIO_E:08X}] & 0x{spi_masks|(3<<22):08X}) != 0x{spi_masks|(1<<22):08X}}} {{error "SPI pin park verification failed"}}',
        f'if {{([mrw 0x{GPIO_E+0x14:08X}] & 0x{NSS:08X}) == 0}} {{error "SPI NSS inactive latch verification failed"}}',
        'wait_halt 1000',
        'echo "TX_ISP_HOLD_READY: STM32 halted, TX powered, SPI parked; keep PB22 low and do not reset STM32"',
        'shutdown',
    ]
    return '\n'.join(lines)+'\n'


def require_single_stlink():
    if sys.platform != 'win32':
        raise ValueError('This entry currently supports Windows ST-LINK only')
    pattern = r'^USB\\VID_0483&PID_(3744|3748|374B|374D|374E|374F|3752|3753|3754|3755|3757)\\'
    result = subprocess.run(['powershell','-NoProfile','-Command',
                            "@(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match '"+pattern+"' }).Count"],
                            capture_output=True,text=True,timeout=15,check=True)
    if result.stdout.strip() != '1':
        raise ValueError('Connect exactly one ST-LINK to the STM32 SWD interface')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--openocd',type=Path)
    parser.add_argument('--target-uid')
    args = parser.parse_args()
    try:
        validate_board()
        script = hold_script(args.target_uid)
        print('Plan: halt STM32; preserve main power; park SPI; power-cycle TX and hold it on.')
        print('Connect PB22 to GND BEFORE execution; do not run during an ISP download.')
        print('No Flash, configuration-word, protection or Standby operations.')
        if not args.execute:
            print('Offline plan checked. Use --execute with ST-LINK connected to STM32.')
            return 0
        require_single_stlink()
        # Reuse installed-binary selection only, not a flash/reset helper.
        from webconfig_flash import resolve_openocd_executable
        executable = resolve_openocd_executable(args.openocd,allow_automatic=True)
        folder = ROOT/'.hbox/tx-isp-hold'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        folder.mkdir(parents=True)
        config=folder/'hold.cfg'; config.write_text(script,encoding='utf-8')
        log=folder/'hold.log'
        started=time.monotonic()
        with log.open('w',encoding='utf-8') as output:
            process=subprocess.Popen([str(executable),'-d1','-f',str(config)],stdout=output,stderr=subprocess.STDOUT)
            print(f'[ISP hold] PID={process.pid} timeout=30s log={log}',flush=True)
            try:
                code=process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                process.terminate(); process.wait(timeout=10)
                raise RuntimeError(f'Hold setup timed out; do not start a download; see {log}')
        print(f'[ISP hold] exit={code} elapsed={time.monotonic()-started:.1f}s')
        if code or 'TX_ISP_HOLD_READY:' not in log.read_text(encoding='utf-8',errors='replace'):
            raise RuntimeError(f'Hold setup failed; do not start a download; see {log}')
        print('STM32 is halted, TX power is held on. Search again in WCHISPStudio.')
        print('Keep PB22 low and do not reset STM32 during maintenance. Afterwards remove boot strap and power-cycle normally.')
        print('未修改任何保护位或锁定状态。No firmware was written.')
        return 0
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print(f'ISP hold failed: {error}',file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
