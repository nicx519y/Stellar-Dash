"""Compile production RF wake handshake with fake endpoints; execution stays opt-in."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from .test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute-authorized', action='store_true',
                        help='Only after the user explicitly resumes RF regression')
    args = parser.parse_args()
    bootstrap = (ROOT / 'application/Src/transport/ch585_role_bootstrap.cpp').read_text(encoding='utf-8')
    ready = (ROOT / 'application/Src/transport/rf/rf_boot_ready.cpp').read_text(encoding='utf-8')
    board = (ROOT / 'application/Inc/system/board_cfg.h').read_text(encoding='utf-8')
    defines = '\n'.join(re.findall(r'^#define (?:CH585_(?:POWER_OFF_MIN_MS|POWER_ON_SETTLE_MS|READY_HINT_TIMEOUT_MS|ROLE_SELECT_RETRY_MS|ROLE_SELECT_TIMEOUT_MS)|RF_BOOT_READY_\w+)\s+[^\r\n]+', board + '\n' + ready, re.M))
    production = defines + '\n'
    production += ready[ready.index('static bool s_readySeen'):ready.index('static void enableGpioClock')]
    production += '\nnamespace RFBootReady {\n'
    for signature in ('void reset()', 'void beginWait()', 'WaitResult pollWait('):
        production += function(ready, signature) + '\n'
    production += '}\n'
    for signature in ('void Ch585RoleBootstrap::shutdown()',
                      'void Ch585RoleBootstrap::beginRfSleepResume()',
                      'Ch585ResumeResult Ch585RoleBootstrap::serviceRfSleepResume()'):
        production += function(bootstrap, signature) + '\n'
    production += function(bootstrap, 'void Ch585RoleBootstrap::setSelector(')
    compiler = shutil.which('g++')
    if not compiler:
        raise RuntimeError('g++ not found')
    with tempfile.TemporaryDirectory(prefix='xora-rf-wake-') as folder:
        temp = Path(folder)
        (temp / 'production.inc').write_text(production, encoding='utf-8')
        exe = temp / 'wake.exe'
        started = time.monotonic()
        print('Compile RF wake production functions (timeout 60 s)', flush=True)
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(temp), '-I' + str(ROOT / 'application/Inc/transport'),
                        '-I' + str(ROOT / 'application/Inc/transport/rf'),
                        str(ROOT / 'tools/tests/rf_wake_bootstrap_test.cpp'), '-o', str(exe)],
                       check=True, timeout=60)
        print(f'Compile passed in {time.monotonic() - started:.1f} s', flush=True)
        if args.execute_authorized:
            subprocess.run([str(exe)], check=True, timeout=10)
            print('RF wake scenarios passed', flush=True)
        else:
            print('RF scenarios NOT executed: regression pause remains in effect', flush=True)


if __name__ == '__main__':
    main()
