from pathlib import Path
import shutil
import tempfile
import unittest
from .application_paths import ROOT, application_include_flags, run_native


class ConfigBootModePersistenceTests(unittest.TestCase):
    def test_real_config_load_save_and_interrupted_commit(self):
        with tempfile.TemporaryDirectory(prefix='xora-config-boot-') as directory:
            temp=Path(directory)
            (temp/'configs').mkdir()
            for name in ('stm32h750xx.h','stm32h7xx_hal.h','utils.h','system_logger.h'):
                (temp/name).write_text('#pragma once\n',encoding='utf-8')
            (temp/'board_cfg.h').write_text('''#pragma once
#include <cmath>
#include "enums.hpp"
#define NUM_ADC_BUTTONS 18
#define NUM_GPIO_BUTTONS 4
#define NUM_PROFILES 16
#define NUM_GAMEPAD_HOTKEYS 11
#define MAX_KEY_COMBINATION 10
#define NUM_LED_AROUND 40
#define FN_BUTTON_VIRTUAL_PIN (1u<<21)
#define CONFIG_VERSION 0x22u
#define CONFIG_ADDR 0x90590000u
#define LOG_STORAGE_ADDR 0x90580000u
#define HBOX_AUTO_SLEEP_ENABLED 0
#define APP_DBG(...) ((void)0)
#define APP_ERR(...) ((void)0)
struct DefaultHotkeyConfig { bool isLocked; GamepadHotkey action; bool isHold; int32_t virtualPin; };
static const DefaultHotkeyConfig DEFAULT_HOTKEY_LIST[11]={};
''',encoding='utf-8')
            (temp/'configs/device_command_handler.hpp').write_text('''#pragma once
#include "config.hpp"
#include <cstdlib>
struct ProfileCommandHandler {
    static cJSON* buildProfileJSON(GamepadProfile*) { std::abort(); }
    static void parseProfileJSON(cJSON*,GamepadProfile*) { std::abort(); }
};
''',encoding='utf-8')
            (temp/'qspi-w25q64.h').write_text('''#pragma once
#include <stdint.h>
#define W25Qxx_PageSize 256u
#define QSPI_W25Qxx_OK 0
bool QSPI_W25Qxx_IsMemoryMappedMode();
int8_t QSPI_W25Qxx_ExitMemoryMappedMode();
int8_t QSPI_W25Qxx_EnterMemoryMappedMode();
int8_t QSPI_W25Qxx_ReadBuffer(uint8_t*,uint32_t,uint32_t);
int8_t QSPI_W25Qxx_BufferErase(uint32_t,uint32_t);
int8_t QSPI_W25Qxx_WritePage(uint8_t*,uint32_t,uint16_t);
''',encoding='utf-8')
            compiler=shutil.which('g++')
            self.assertIsNotNone(compiler)
            exe=temp/'config-boot.exe'
            run_native([compiler,'-std=c++17','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                        '-I'+str(temp),'-I'+str(ROOT/'application/Libs/cJSON'),*application_include_flags(),
                        str(ROOT/'application/Src/config/config.cpp'),
                        str(ROOT/'application/Src/config/storagemanager.cpp'),
                        str(ROOT/'application/Libs/cJSON/cJSON.c'),
                        str(ROOT/'tools/tests/config_boot_mode_persistence_test.cpp'),'-o',str(exe)],check=True)
            run_native([str(exe)],check=True)
