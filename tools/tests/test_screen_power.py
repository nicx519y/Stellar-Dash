"""Compile production configuration and menu dispatch with hardware-free leaf stubs."""
import re
import shutil
import tempfile
import unittest
from pathlib import Path
from .application_paths import ROOT, application_include_flags, run_native


class ScreenPowerTests(unittest.TestCase):
    def test_config_and_group_navigation(self):
        with tempfile.TemporaryDirectory(prefix="xora-screen-power-") as directory:
            temp = Path(directory)
            # Keep the complete production Config declarations; only MCU headers are stubbed.
            for name in ("stm32h750xx.h", "stm32h7xx_hal.h"):
                (temp / name).write_text("#pragma once\n#include <cmath>\n#include <cstdint>\ntypedef struct {} TIM_HandleTypeDef;\n", encoding="utf-8")
            (temp / "board_cfg.h").write_text("#pragma once\n#define NUM_ADC_BUTTONS 18\n#define NUM_GPIO_BUTTONS 4\n#define NUM_PROFILES 8\n#define NUM_GAMEPAD_HOTKEYS 11\n#define MAX_KEY_COMBINATION 16\n", encoding="utf-8")
            (temp / "storagemanager.hpp").write_text('#pragma once\n#include "config.hpp"\nstruct StorageStub { Config config = {}; };\nextern StorageStub STORAGE_MANAGER;\n', encoding="utf-8")
            (temp / "board_mode.hpp").write_text('#pragma once\nstruct BoardStub { bool allowed = true; bool isWebConfigAllowed() const { return allowed; } };\nextern BoardStub BOARD_MODE;\n', encoding="utf-8")
            header = (ROOT / "application/Inc/display/screen_control/spi_screen_detail_entries.hpp").read_text(encoding="utf-8")
            stubs = '#include "screen_control/spi_screen_detail_entries.hpp"\n'
            for result, declaration in re.findall(r'^(void|bool|uint8_t) (ScreenDetail[^;]+);$', header, re.M):
                stubs += result + ' ' + declaration + (' {}\n' if result == 'void' else ' { return 0; }\n')
            (temp / "leaves.cpp").write_text(stubs, encoding="utf-8")
            for enabled in (0, 1):
                exe = temp / f"screen-power-{enabled}.exe"
                run_native([shutil.which("g++"), "-std=c++17", f"-DHBOX_AUTO_SLEEP_ENABLED={enabled}", "-I" + str(temp),
                            *application_include_flags(), "-I" + str(ROOT / "common"), "-I" + str(ROOT / "application/Libs/cJSON"),
                            "-I" + str(ROOT / "application/Inc/display/drivers/st7789"),
                            str(ROOT / "tools/tests/screen_power_config_test.cpp"), str(temp / "leaves.cpp"),
                            str(ROOT / "application/Src/display/screen_control/spi_screen_detail_pages.cpp"),
                            str(ROOT / "application/Src/display/screen_control/spi_screen_main_list.cpp"),
                            str(ROOT / "application/Libs/cJSON/cJSON.c"), "-o", str(exe)], check=True)
                run_native([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
