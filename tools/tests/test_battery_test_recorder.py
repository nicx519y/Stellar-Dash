from pathlib import Path
import shutil
import tempfile
import unittest
import subprocess
from tools.tests.fault_log_test_support import run_checked

ROOT = Path(__file__).resolve().parents[2]


class BatteryTestRecorderTests(unittest.TestCase):
    def test_memory_mapped_clear_and_failure_recovery(self):
        with tempfile.TemporaryDirectory(prefix="xora-battery-mapped-", ignore_cleanup_errors=True) as folder:
            temp = Path(folder)
            for name in ("power_manager.hpp", "storagemanager.hpp", "screen_control_config.hpp",
                         "system_sleep_manager.hpp", "qspi-w25q64.h", "firmware_metadata.h", "stm32h7xx_hal.h"):
                (temp / name).write_text('#pragma once\n#include "battery_test_runtime_stubs.hpp"\n', encoding="utf-8")
            exe = str(temp / "mapped.exe")
            run_checked([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                         "-Wno-int-to-pointer-cast", "-DXORA_BATTERY_TEST_REQUIRED=1",
                         "-I" + str(temp), "-I" + str(ROOT / "common"),
                         "-I" + str(ROOT / "tools/tests"), "-I" + str(ROOT / "application/Inc/power"),
                         str(ROOT / "tools/tests/battery_test_mapped_test.cpp"),
                         str(ROOT / "application/Src/power/battery_test.cpp"), "-o", exe], timeout=120)
            run_checked([exe], timeout=30)

    def test_continuous_screen_refresh_does_not_starve_clear(self):
        from tools.tests.test_ch585_usb_handshake import function
        main = (ROOT / "application/Src/system/main_state_machine.cpp").read_text(encoding="utf-8")
        screen = (ROOT / "application/Src/display/screen_control/spi_screen_manager.cpp").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory(prefix="xora-battery-dispatch-", ignore_cleanup_errors=True) as folder:
            temp = Path(folder)
            for name in ("power_manager.hpp", "storagemanager.hpp", "screen_control_config.hpp",
                         "system_sleep_manager.hpp", "qspi-w25q64.h", "firmware_metadata.h", "stm32h7xx_hal.h"):
                (temp / name).write_text('#pragma once\n#include "battery_test_runtime_stubs.hpp"\n', encoding="utf-8")
            (temp / "battery_dispatch_production.inc").write_text(
                function(screen, 'extern "C" int BatteryTest_DisplayReady(void)') + "\n" +
                function(main, "void MainStateMachine::serviceSharedRuntime()"), encoding="utf-8")
            exe = str(temp / "dispatch.exe")
            run_checked([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-address",
                         "-Wno-int-to-pointer-cast", "-DXORA_BATTERY_TEST_REQUIRED=1",
                         "-I" + str(temp), "-I" + str(ROOT / "common"),
                         "-I" + str(ROOT / "tools/tests"), "-I" + str(ROOT / "application/Inc/power"),
                         str(ROOT / "tools/tests/battery_test_dispatch_test.cpp"),
                         str(ROOT / "application/Src/power/battery_test.cpp"), "-o", exe], timeout=120)
            run_checked([exe], timeout=30)

    def test_production_runtime(self):
        with tempfile.TemporaryDirectory(prefix="xora-battery-runtime-", ignore_cleanup_errors=True) as folder:
            temp = Path(folder)
            for name in ("power_manager.hpp", "storagemanager.hpp", "screen_control_config.hpp",
                         "system_sleep_manager.hpp", "qspi-w25q64.h", "firmware_metadata.h", "stm32h7xx_hal.h"):
                (temp / name).write_text('#pragma once\n#include "battery_test_runtime_stubs.hpp"\n', encoding="utf-8")
            exe = str(temp / "runtime.exe")
            run_checked([shutil.which("g++"), "-g", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                         "-Wno-int-to-pointer-cast", "-DXORA_BATTERY_TEST_REQUIRED=1",
                         "-I" + str(temp), "-I" + str(ROOT / "common"),
                         "-I" + str(ROOT / "tools/tests"), "-I" + str(ROOT / "application/Inc/power"),
                         str(ROOT / "tools/tests/battery_test_runtime_test.cpp"),
                         str(ROOT / "application/Src/power/battery_test.cpp"), "-o", exe], timeout=120)
            try:
                run_checked([exe], timeout=120)
            except subprocess.CalledProcessError:
                if shutil.which("gdb"):
                    run_checked([shutil.which("gdb"), "-batch", "-ex", "run", "-ex", "bt", exe], timeout=30)
                raise

    def test_production_recorder_and_partition(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="xora-battery-recorder-") as folder:
            exe = str(Path(folder) / "test.exe")
            run_checked([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                         "-I" + str(ROOT / "common"),
                         "-I" + str(ROOT / "application/Inc/power"),
                         str(ROOT / "tools/tests/battery_test_recorder_test.cpp"),
                         "-o", exe], timeout=120)
            run_checked([exe], timeout=120)
