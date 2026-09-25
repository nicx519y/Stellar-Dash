import shutil
import os
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run_checked(command):
    process = subprocess.Popen(command, start_new_session=os.name != 'nt')
    print(f'  PID={process.pid}, timeout=120s', flush=True)
    try:
        code = process.wait(timeout=120)
    except subprocess.TimeoutExpired:
        if os.name == 'nt':
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], timeout=15)
        else:
            os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=15)
        raise
    if code:
        raise subprocess.CalledProcessError(code, command)


class AutoSleepTests(unittest.TestCase):
    def test_screen_standby_after_short_wake_press(self):
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='hbox-screen-wake-') as folder:
            temp = Path(folder)
            stubs = {
                'stm32h7xx_hal.h': 'typedef struct {} TIM_HandleTypeDef;',
                'stm32h7xx.h': '''#include <stdint.h>
inline void SCB_InvalidateDCache_by_Addr(uint32_t*, int32_t) {}
inline void __DSB() {}
inline void __ISB() {}''',
                'qspi-w25q64.h': '''#define QSPI_W25Qxx_OK 0
inline bool QSPI_W25Qxx_IsMemoryMappedMode() { return false; }
inline int QSPI_W25Qxx_EnterMemoryMappedMode() { return -1; }''',
                'system_logger.h': '#define LOG_WARN(...) ((void)0)',
                'board_cfg.h': '''#define ST7789_WIDTH 320u
#define ST7789_HEIGHT 172u
#define USER_IMAGE_RESOURCES_ADDR 0u
#define USER_IMAGE_RESOURCES_SIZE 1048576u
#define BOARD_WIDTH 320u
#define NUM_ADC_BUTTONS 1u
#define NUM_GPIO_BUTTONS 1u
static const struct { float x, y, r; } HITBOX_BUTTON_POS_LIST[2] = {};''',
            }
            for name, body in stubs.items():
                (temp / name).write_text('#pragma once\n' + body, encoding='utf-8')
            exe = temp / 'screen-wake.exe'
            print('screen wake: compiling real screen standby with fake display/QSPI', flush=True)
            run_checked([compiler, '-std=c++17', '-Wall', '-Wextra',
                         '-Wno-int-to-pointer-cast', '-I', str(temp),
                         '-I', str(ROOT / 'application/Cpp_Core/Inc'),
                         '-I', str(ROOT / 'application/Drivers/SPI-ST7789'),
                         '-I', str(ROOT / 'application/Libs/CRC32/src'),
                         str(ROOT / 'application/Libs/CRC32/src/CRC32.cpp'),
                         str(ROOT / 'application/Cpp_Core/Src/screen_control/spi_screen_standby.cpp'),
                         str(ROOT / 'tools/tests/screen_sleep_wake_test.cpp'), '-o', str(exe)])
            run_checked([str(exe)])

    def test_production_manager_with_fake_peripherals(self):
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='hbox-auto-sleep-runtime-') as folder:
            temp = Path(folder)
            for name in ('board_cfg.h', 'board_power.hpp', 'board_mode.hpp',
                         'states/input_state.hpp', 'screen_control/spi_screen_manager.hpp',
                         'leds/leds_manager.hpp', 'storagemanager.hpp', 'connection_manager.hpp',
                         'rf_bridge_port.hpp', 'power_manager.hpp', 'rotary-encoder.h', 'stm32h7xx_hal.h',
                         'stm32h7xx_hal_pwr_ex.h', 'system_logger.h'):
                path = temp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#pragma once\n', encoding='utf-8')
            exe = temp / 'runtime.exe'
            started = time.monotonic()
            print('auto-sleep runtime: compiling production manager with fake peripherals', flush=True)
            run_checked([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-DHBOX_AUTO_SLEEP_ENABLED=1', '-I', str(temp),
                            '-I', str(ROOT / 'application/Cpp_Core/Inc'),
                            '-include', str(ROOT / 'tools/tests/auto_sleep_runtime_stubs.hpp'),
                            str(ROOT / 'tools/tests/auto_sleep_runtime_test.cpp'),
                            str(ROOT / 'application/Cpp_Core/Src/system_sleep_manager.cpp'),
                            '-o', str(exe)])
            for scenario in ('cycles', 'reset', 'boot-key', 'exclusive', 'prepare-timeout',
                             'mode-change', 'restore-failure', 'keepalive-failure',
                             'cancel-before-pause', 'pause-failure', 'reset-pending', 'noise',
                             'disabled', 'retime', 'software', 'fault', 'debugger',
                             'disable-prepare', 'disable-sleep', 'disable-restore',
                             'stop-fault', 'stop-failure', 'stop-short-key',
                             'xinput-idle', 'xinput-neutral-failure', 'xinput-cycles'):
                print(f'auto-sleep runtime: {scenario}', flush=True)
                run_checked([str(exe), scenario])
            print(f'auto-sleep runtime: passed in {time.monotonic() - started:.2f}s', flush=True)

    def test_stop_timer_and_power_maintenance(self):
        with tempfile.TemporaryDirectory(prefix='xora-stop-timing-') as folder:
            temp = Path(folder)
            (temp / 'stm32h7xx_hal.h').write_text(
                '#pragma once\n#include <stdint.h>\ntypedef struct {} I2C_HandleTypeDef;\n',
                encoding='utf-8')
            exe = temp / 'timing.exe'
            run_checked([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                         '-I', str(temp), '-I', str(ROOT / 'application/Cpp_Core/Inc'),
                         '-I', str(ROOT / 'application/Drivers/I2C-BQ25895'),
                         '-I', str(ROOT / 'application/Drivers/I2C-MAX17048'),
                         str(ROOT / 'tools/tests/stop_timer_timing_test.cpp'), '-o', str(exe)])
            run_checked([str(exe)])

    def test_config_migration_dispatch(self):
        source = (ROOT / 'application/Cpp_Core/Src/config.cpp').read_text(encoding='utf-8')
        # 0x21 must hit the preserving migration branch, never factory defaults.
        branch = source.split('} else if (fjResult == true &&', 1)[1].split('} else if', 1)[0]
        self.assertIn('CONFIG_VERSION_AUTO_SLEEP_MIGRATE_FROM', branch)
        self.assertIn('config.version = CONFIG_VERSION;', branch)
        self.assertIn('return save(config);', branch)
        self.assertNotIn('makeDefaultProfile', branch)
        self.assertIn('if (fjResult && config.version < CONFIG_VERSION) migrate_legacy_power_config(config.power);', source)

    def test_power_config_and_lcd_resume(self):
        compiler = shutil.which('g++')
        with tempfile.TemporaryDirectory(prefix='hbox-power-config-') as folder:
            temp = Path(folder)
            (temp / 'board_cfg.h').write_text('#pragma once\n', encoding='utf-8')
            includes = ['-I', str(temp), '-I', str(ROOT / 'application/Cpp_Core/Inc'),
                        '-I', str(ROOT / 'application/Libs/cJSON'),
                        '-I', str(ROOT / 'application/Drivers/SPI-ST7789')]
            for enabled in (0, 1):
                exe = temp / f'power-{enabled}.exe'
                run_checked([compiler, '-std=c++17', f'-DHBOX_AUTO_SLEEP_ENABLED={enabled}',
                             *includes, str(ROOT / 'tools/tests/power_config_test.cpp'),
                             str(ROOT / 'application/Libs/cJSON/cJSON.c'), '-o', str(exe)])
                run_checked([str(exe)])
            exe = temp / 'lcd.exe'
            run_checked([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *includes,
                         str(ROOT / 'tools/tests/lcd_resume_test.cpp'),
                         str(ROOT / 'application/Drivers/SPI-ST7789/spi-st7789-resume.c'),
                         '-o', str(exe)])
            run_checked([str(exe)])

    def test_runtime_policy(self):
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler, 'host g++ is required')
        with tempfile.TemporaryDirectory(prefix='hbox-auto-sleep-') as folder:
            exe = Path(folder) / 'auto_sleep.exe'
            commands = [
                ('compile', [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                             '-I', str(ROOT / 'application/Cpp_Core/Inc'),
                             str(ROOT / 'tools/tests/auto_sleep_policy_test.cpp'), '-o', str(exe)]),
                ('execute', [str(exe)]),
            ]
            for phase, command in commands:
                started = time.monotonic()
                print(f'auto-sleep {phase}: begin', flush=True)
                run_checked(command)
                print(f'auto-sleep {phase}: passed in {time.monotonic() - started:.2f}s', flush=True)

    def test_no_persistent_or_standby_side_effects(self):
        source = (ROOT / 'application/Cpp_Core/Src/system_sleep_manager.cpp').read_text(encoding='utf-8')
        for forbidden in ('HAL_PWR_EnterSTANDBYMode(', 'HAL_PWREx_EnterSTOPMode(',
                          'HAL_PWR_EnterSTOPMode(', 'saveConfig(', 'setBootMode(',
                          'prepareForStandby(', 'prepareSystemSleep(',
                          'HAL_FLASH_', 'HAL_RTCEx_BKUPWrite(', 'NVIC_SystemReset(',
                          '__WFE('):
            self.assertNotIn(forbidden, source)
        self.assertNotIn('setCh585Enabled(false)', source)
        self.assertIn('SystemStop_Enter(', source.split('void SystemSleep_Idle(void)')[1])
        stop = (ROOT / 'application/Cpp_Core/Src/system_stop.cpp').read_text(encoding='utf-8')
        for forbidden in ('HAL_PWR_EnterSTANDBYMode(', 'HAL_FLASH_', 'HAL_RTCEx_BKUPWrite('):
            self.assertNotIn(forbidden, stop)
        self.assertEqual(stop.count('__WFI()'), 1)
        self.assertIn('PWR_CPUCR_PDDS_D3 | PWR_CPUCR_RUN_D3', stop)
        self.assertIn('SCB_CleanDCache_by_Addr', stop)
        config = (ROOT / 'application/Makefile').read_text(encoding='utf-8')
        self.assertIn('HBOX_AUTO_SLEEP_ENABLED ?= 1', config)
        board = (ROOT / 'application/Core/Inc/board_cfg.h').read_text(encoding='utf-8')
        self.assertIn('#ifndef HBOX_AUTO_SLEEP_ENABLED\n#define HBOX_AUTO_SLEEP_ENABLED 1', board)

    def test_stop_restores_run_voltage_before_fast_clocks(self):
        # Source-order contract, not a simulation of regulator readiness.
        # STOP clears Run VOS to VOS3 even when VOSRDY is already set.
        stop = (ROOT / 'application/Cpp_Core/Src/system_stop.cpp').read_text(encoding='utf-8')
        save = stop.index('const uint32_t oldVos = PWR->D3CR & PWR_D3CR_VOS;')
        sleep = stop.index('__WFI()')
        restore = stop.index('MODIFY_REG(PWR->D3CR, PWR_D3CR_VOS, oldVos);')
        selected = stop.index('waitBits(PWR->D3CR, PWR_D3CR_VOS | PWR_D3CR_VOSRDY,', restore)
        actual = stop.index('waitBits(PWR->CSR1, PWR_CSR1_ACTVOS | PWR_CSR1_ACTVOSRDY,', selected)
        boost = stop.index('SET_BIT(SYSCFG->PWRCR, SYSCFG_PWRCR_ODEN);', actual)
        boost_ready = stop.index('waitBits(PWR->D3CR, PWR_D3CR_VOSRDY, PWR_D3CR_VOSRDY)', boost)
        plls = stop.index('SET_BIT(RCC->CR, plls);', boost_ready)
        fast_clock = stop.index('MODIFY_REG(RCC->CFGR, RCC_CFGR_SW | RCC_CFGR_STOPWUCK,', plls)
        self.assertEqual(sorted((save, sleep, restore, selected, actual, boost,
                                 boost_ready, plls, fast_clock)),
                         [save, sleep, restore, selected, actual, boost,
                          boost_ready, plls, fast_clock])
        self.assertIn('oldVos | PWR_D3CR_VOSRDY)) recoveryReset();', stop[selected:actual])
        self.assertIn('actualVos | PWR_CSR1_ACTVOSRDY)) recoveryReset();', stop[actual:boost])


if __name__ == '__main__':
    unittest.main()
