"""Compile software selection, migration and the real screen action without devices."""
import shutil
import tempfile
import unittest
from pathlib import Path
from .application_paths import ROOT, application_include_flags, run_native


class SoftwareConnectionTests(unittest.TestCase):
    def test_migration_and_screen_transaction(self):
        with tempfile.TemporaryDirectory(prefix="xora-connection-") as directory:
            temp = Path(directory)
            (temp / "states").mkdir()
            for name in ("stm32h750xx.h", "stm32h7xx_hal.h"):
                (temp / name).write_text("#pragma once\n#include <cmath>\n#include <cstdint>\nstruct TIM_HandleTypeDef {};\n", encoding="utf-8")
            (temp / "board_cfg.h").write_text("#pragma once\n#define NUM_ADC_BUTTONS 18\n#define NUM_GPIO_BUTTONS 4\n#define NUM_PROFILES 8\n#define NUM_GAMEPAD_HOTKEYS 11\n#define MAX_KEY_COMBINATION 16\n", encoding="utf-8")
            (temp / "storagemanager.hpp").write_text('''#pragma once
#include "connection_mode_config.hpp"
#include <cassert>
inline bool running=true, saveOk=true, isp=false;
inline int saves=0, resets=0;
struct StorageStub {
    Config config={}, committed={};
    BootMode getBootMode() { return config.bootMode; }
    void setBootMode(BootMode mode) { config.bootMode=mode; }
    void setConnectionMode(ConnectionMode mode) { config.connectionMode=mode; }
    bool saveConfig() {
        assert(!running); ++saves;
        ConnectionConfig::normalize(config);
        if(saveOk) committed=config;
        else config.screenControl.reservedStyle[0]=ScreenConfig::kPreviousSchema;
        return saveOk;
    }
};
inline StorageStub STORAGE_MANAGER;
''', encoding="utf-8")
            (temp / "states/input_state.hpp").write_text('''#pragma once
#include "storagemanager.hpp"
struct InputStub {
    bool suspendInputPipelineForStorage() { bool previous=running; running=false; return previous; }
    bool resumeInputPipelineAfterStorage(bool previous) { running=previous; return true; }
};
inline InputStub INPUT_STATE;
''', encoding="utf-8")
            exe = temp / "connection.exe"
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra",
                        "-I" + str(temp), *application_include_flags(),
                        "-I" + str(ROOT / "common"),
                        "-I" + str(ROOT / "application/Libs/cJSON"),
                        "-I" + str(ROOT / "application/Inc/display/drivers/st7789"),
                        str(ROOT / "tools/tests/software_connection_mode_test.cpp"),
                        str(ROOT / "application/Src/system/board_mode.cpp"),
                        str(ROOT / "application/Src/display/screen_control/spi_screen_detail_connection.cpp"),
                        "-o", str(exe)], check=True)
            run_native([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
