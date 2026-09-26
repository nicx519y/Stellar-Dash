"""Compile-only by default. RF execution remains paused by repository policy."""
import argparse
import shutil
import tempfile
from pathlib import Path

from tools.tests.test_auto_sleep import ROOT, run_checked

try:
    from .application_paths import application_include_flags
except ImportError:
    from application_paths import application_include_flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute-authorized', action='store_true',
                        help='Only after the user explicitly resumes RF regression')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='xora-rf-sleep-') as folder:
        temp = Path(folder)
        for name in ('rf_bridge_port.hpp', 'usb_board_link_port.hpp', 'usb_board_link.hpp',
                     'ch585_role_bootstrap.hpp', 'connection_manager.hpp', 'stm32h7xx_hal.h'):
            (temp / name).write_text('#pragma once\n', encoding='utf-8')
        executable = temp / 'rf-sleep.exe'
        run_checked([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                     '-I', str(temp), *application_include_flags(),
                     '-include', str(ROOT / 'tools/tests/rf_sleep_recovery_stubs.hpp'),
                     str(ROOT / 'application/Src/transport/rf/rf_sleep_recovery.cpp'),
                     str(ROOT / 'tools/tests/rf_sleep_recovery_test.cpp'), '-o', str(executable)])
        print('RF recovery production state machine: compile passed', flush=True)
        local = temp / 'local'
        local.mkdir()
        for name in ('board_cfg.h', 'board_power.hpp', 'board_mode.hpp', 'states/input_state.hpp',
                     'screen_control/spi_screen_manager.hpp', 'leds/leds_manager.hpp',
                     'storagemanager.hpp', 'power_manager.hpp', 'connection_manager.hpp', 'rf_bridge_port.hpp',
                     'rotary-encoder.h', 'stm32h7xx_hal.h', 'stm32h7xx_hal_pwr_ex.h', 'system_logger.h'):
            header = local / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#pragma once\n', encoding='utf-8')
        local_exe = local / 'local-resume.exe'
        run_checked([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                     '-DHBOX_AUTO_SLEEP_ENABLED=1', '-I', str(local),
                     *application_include_flags(),
                     '-include', str(ROOT / 'tools/tests/auto_sleep_runtime_stubs.hpp'),
                     str(ROOT / 'application/Src/power/system_sleep_manager.cpp'),
                     str(ROOT / 'tools/tests/rf_sleep_local_runtime_test.cpp'), '-o', str(local_exe)])
        print('RF local independence / first-key gate: compile passed', flush=True)
        event_dir = temp / 'event'
        event_dir.mkdir()
        (event_dir / 'board_cfg.h').write_text('#pragma once\n', encoding='utf-8')
        (event_dir / 'stm32h7xx_hal.h').write_text(
            '#pragma once\n#include <stdint.h>\nextern uint32_t testReliableNow;\n'
            'inline uint32_t HAL_GetTick() { return testReliableNow; }\n', encoding='utf-8')
        event_exe = event_dir / 'reliable-payload.exe'
        run_checked([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                     '-I', str(event_dir), *application_include_flags(),
                     str(ROOT / 'application/Src/transport/rf/rf_reliable_event.cpp'),
                     str(ROOT / 'tools/tests/rf_reliable_event_payload_test.cpp'), '-o', str(event_exe)])
        print('RF real reliable-event decoder/queue, 23/24/25-byte status: compile passed', flush=True)
        if not args.execute_authorized:
            print('RF scenarios NOT executed: regression pause remains in effect', flush=True)
            return
        run_checked([str(local_exe)])
        run_checked([str(event_exe)])
        for scenario in ('success', 'offline', 'fallback', 'retry', 'fresh-status',
                         'unsafe-park', 'cancel', 'old-session', 'cycles',
                         'handoff', 'cancel-handoff', 'handoff-wrap'):
            print(scenario, flush=True)
            run_checked([str(executable), scenario])


if __name__ == '__main__':
    main()
