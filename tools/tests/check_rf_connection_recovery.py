"""Compile production RF manager scenarios; execution requires explicit RF authorization."""
import argparse
import shutil
import tempfile
from pathlib import Path

from tools.tests.test_auto_sleep import ROOT, run_checked
from tools.tests.test_rf_runtime_recovery import function


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute-authorized', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'application/Src/transport/connection_manager.cpp').read_text(encoding='utf-8')
    names = ('confirmRfReportRate', 'serviceRfStatusPoll', 'serviceRfRuntimeRecovery', 'onReportReady')
    with tempfile.TemporaryDirectory(prefix='xora-rf-manager-') as folder:
        temp = Path(folder)
        (temp / 'rf_manager_functions.inc').write_text(
            '\n'.join(function(source, name) for name in names), encoding='utf-8')
        exe = temp / 'rf-manager.exe'
        run_checked([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                     '-I', str(temp), str(ROOT / 'tools/tests/rf_connection_recovery_test.cpp'),
                     '-o', str(exe)])
        print('RF manager production routines: compile passed', flush=True)
        if args.execute_authorized:
            run_checked([str(exe)])
        else:
            print('RF scenarios NOT executed: regression pause remains in effect', flush=True)


if __name__ == '__main__':
    main()
