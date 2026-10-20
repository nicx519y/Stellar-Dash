"""Battery-only host simulation; no RF code or hardware access is executed."""
from pathlib import Path
import shutil
import tempfile
import unittest
from tools.tests.application_paths import application_include_flags, run_native

ROOT = Path(__file__).resolve().parents[2]


class BatteryManagementTests(unittest.TestCase):
    def test_battery_and_notice_policies(self):
        with tempfile.TemporaryDirectory(prefix="xora-battery-policy-") as folder:
            exe = Path(folder) / "policy.exe"
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *application_include_flags(),
                        str(ROOT / "tools/tests/battery_policy_test.cpp"), "-o", str(exe)], check=True)
            run_native([str(exe)], check=True)

    def test_production_power_manager(self):
        with tempfile.TemporaryDirectory(prefix="xora-battery-") as folder:
            temp = Path(folder)
            for name in ("board_cfg.h", "board_mode.hpp", "board_power.hpp",
                         "ch585_role_bootstrap.hpp", "connection_manager.hpp",
                         "storagemanager.hpp", "system_sleep_manager.hpp"):
                (temp / name).write_text("#pragma once\n", encoding="utf-8")
            (temp / "stm32h7xx_hal.h").write_text(
                '#pragma once\n#include <stdint.h>\n'
                'typedef struct {} I2C_HandleTypeDef;\n', encoding="utf-8")
            exe = temp / "battery.exe"
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra",
                        "-Wno-missing-field-initializers", "-Werror",
                        "-I", str(temp), *application_include_flags(),
                        str(ROOT / "tools/tests/battery_manager_test.cpp"), "-o", str(exe)],
                       check=True)
            run_native([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
