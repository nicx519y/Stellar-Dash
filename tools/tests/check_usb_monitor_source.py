"""Compile production USB monitor discovery scenarios without running paused regressions."""
import argparse
import shutil
import tempfile
from pathlib import Path

from tools.tests.test_auto_sleep import ROOT, run_checked
from tools.tests.test_ch585_usb_handshake import function


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute-authorized', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'application/Src/transport/usb/usb_board_link.cpp').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='xora-usb-monitor-source-') as folder:
        temp = Path(folder)
        (temp / 'usb_monitor_state.inc').write_text(
            function(source, 'struct UsbSourceMonitor') + ';\n', encoding='utf-8')
        (temp / 'usb_monitor_functions.inc').write_text('\n'.join(
            function(source, signature) for signature in (
                'bool consumeUsbMonitorEvent(', 'void UsbBoardLink::pumpMonitor(')), encoding='utf-8')
        exe = temp / 'usb-monitor-source.exe'
        run_checked([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                     '-I', str(temp), '-I', str(ROOT / 'common'),
                     str(ROOT / 'tools/tests/usb_monitor_source_test.cpp'), '-o', str(exe)])
        print('USB monitor production discovery routines: compile passed', flush=True)
        if args.execute_authorized:
            run_checked([str(exe)])
        else:
            print('Scenarios NOT executed: monitor regression pause remains in effect', flush=True)


if __name__ == '__main__':
    main()
