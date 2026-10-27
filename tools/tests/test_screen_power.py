"""Exercise menu navigation/actions with production leaves and hardware-free services."""
import re
import shutil
import tempfile
import unittest
from pathlib import Path
from .application_paths import ROOT, application_include_flags, run_native
from .test_ch585_usb_handshake import function


class ScreenPowerTests(unittest.TestCase):
    def test_saved_first_menu_and_live_cursor(self):
        source = (ROOT / 'application/Src/display/screen_control/spi_screen_manager.cpp').read_text(encoding='utf-8')
        code = r'''
#include <cstdint>
#include <cassert>
#include <cstring>
#include <vector>
struct Config { struct { uint16_t currentPageId; } screenControl; };
struct { Config config; } STORAGE_MANAGER;
std::vector<uint8_t> available={15,3,0,20};
uint8_t ScreenMain_RebuildMenuIds(const decltype(Config::screenControl)&, uint8_t* out, uint8_t cap) {
    assert(available.size()<=cap); memcpy(out,available.data(),available.size()); return available.size();
}
struct MenuBootHarness { uint8_t menuCount=0,menuIndex=0,menuIds[32]={}; void rebuildMenu(); };
'''
        code += function(source, 'void SPIScreenManager::rebuildMenu()').replace('SPIScreenManager::', 'MenuBootHarness::')
        code += r'''
int main() {
    for (uint16_t requested : {0,15,20,255}) {
        STORAGE_MANAGER.config.screenControl.currentPageId=requested;
        MenuBootHarness menu; menu.rebuildMenu();
        assert(menu.menuIds[menu.menuIndex]==(requested==255 ? 15 : requested));
    }
    STORAGE_MANAGER.config.screenControl.currentPageId=20;
    MenuBootHarness menu; menu.rebuildMenu(); menu.menuIndex=1; // User navigated to Report Rate.
    available={3,15,0,20}; menu.rebuildMenu();
    assert(menu.menuIndex==0 && menu.menuIds[menu.menuIndex]==3); // Reordering retains cursor.
    available={15,20}; menu.rebuildMenu(); assert(menu.menuIndex==0);
    available.clear(); menu.rebuildMenu(); assert(menu.menuCount==0 && menu.menuIndex==0);
}
'''
        with tempfile.TemporaryDirectory(prefix='xora-screen-boot-menu-') as directory:
            cpp, exe = Path(directory) / 'test.cpp', Path(directory) / 'test.exe'
            cpp.write_text(code, encoding='utf-8')
            run_native([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
            run_native([str(exe)], check=True)

    def test_config_and_group_navigation(self):
        with tempfile.TemporaryDirectory(prefix="xora-screen-power-") as directory:
            temp = Path(directory)
            # Keep the complete production Config declarations; only MCU headers are stubbed.
            for name in ("stm32h750xx.h", "stm32h7xx_hal.h"):
                (temp / name).write_text("#pragma once\n#include <cmath>\n#include <cstdint>\ntypedef struct {} TIM_HandleTypeDef;\n", encoding="utf-8")
            (temp / "board_cfg.h").write_text("#pragma once\n#define NUM_ADC_BUTTONS 18\n#define NUM_GPIO_BUTTONS 4\n#define NUM_PROFILES 8\n#define NUM_GAMEPAD_HOTKEYS 11\n#define MAX_KEY_COMBINATION 16\n", encoding="utf-8")
            (temp / "storagemanager.hpp").write_text('''
#pragma once
#include "config.hpp"
#include <cstdio>
struct StorageStub {
 Config config = {};
 unsigned profileWrites=0, inputWrites=0, bootWrites=0, commits=0;
 GamepadProfile* getDefaultGamepadProfile() { return &config.profiles[0]; }
 void setDefaultProfileId(const char* id) { ++profileWrites; snprintf(config.defaultProfileId, sizeof(config.defaultProfileId), "%s", id); }
 InputMode getInputMode() { return config.inputMode; }
 void setInputMode(InputMode value) { ++inputWrites; config.inputMode=value; }
 void setBootMode(BootMode value) { ++bootWrites; config.bootMode=value; }
 bool saveConfig() { ++commits; return true; }
};
extern StorageStub STORAGE_MANAGER;
''', encoding="utf-8")
            (temp / "board_mode.hpp").write_text('#pragma once\nenum class BoardMode { Usb, Rf };\nstruct BoardStub { bool isStable() const { return true; } BoardMode current() const { return BoardMode::Usb; } bool allowed = true; bool isWebConfigAllowed() const { return allowed; } };\nextern BoardStub BOARD_MODE;\n', encoding="utf-8")
            (temp / "adc_btns").mkdir()
            (temp / "adc_btns/adc_btns_worker.hpp").write_text(
                "#pragma once\nstruct AdcStub { unsigned setups=0; void setup() { ++setups; } }; inline AdcStub ADC_BTNS_WORKER;\n", encoding="utf-8")
            (temp / "leds").mkdir()
            (temp / "leds/leds_manager.hpp").write_text('''
#pragma once
#include "storagemanager.hpp"
struct LedStub {
 void enableSwitch() { auto& v = STORAGE_MANAGER.config.profiles[0].ledsConfigs.ledEnabled; v = !v; }
 void ambientLightEnableSwitch() { auto& v = STORAGE_MANAGER.config.profiles[0].ledsConfigs.aroundLedEnabled; v = !v; }
 void setLedsBrightness(uint8_t) {}
 void setAmbientLightBrightness(uint8_t) {}
};
inline LedStub LEDS_MANAGER;
''', encoding="utf-8")
            (temp / "leds/lighting_resources.hpp").write_text('''
#pragma once
#include "config.hpp"
#include "xora_resource_store.hpp"
namespace LightingResources {
inline XoraResource::Ref selected[2] = {};
inline unsigned selections=0;
inline XoraResource::Ref reference(const GamepadProfile*, bool ambient) { return selected[ambient]; }
inline bool select(GamepadProfile*, bool ambient, const XoraResource::Ref& ref, bool) {
 ++selections; selected[ambient]=ref; return true;
}
inline unsigned list(bool, XoraResource::Ref* refs, const char** labels, unsigned) {
 memset(refs,0,2*sizeof(*refs)); reinterpret_cast<unsigned char*>(&refs[1])[0]=1;
 labels[0]="Effect A"; labels[1]="Effect B"; return 2;
}
}
''', encoding="utf-8")
            real_leaves = {"ButtonsPerformance": "buttons_performance", "LightBrightness": "light_brightness",
                           "AmbientBrightness": "ambient_brightness", "ScreenBrightness": "screen_brightness",
                           "Profiles": "profiles", "Socd": "socd", "InputMode": "input_mode",
                           "LightEffect": "light_effect", "AmbientEffect": "ambient_effect"}
            header = (ROOT / "application/Inc/display/screen_control/spi_screen_detail_entries.hpp").read_text(encoding="utf-8")
            stubs = '#include "screen_control/spi_screen_detail_entries.hpp"\n'
            for result, declaration in re.findall(r'^(void|bool|uint8_t|const char\*) (ScreenDetail[^;]+);$', header, re.M):
                if any(declaration.startswith('ScreenDetail' + name + '_') for name in real_leaves):
                    continue
                stubs += result + ' ' + declaration + (' {}\n' if result == 'void' else ' { return 0; }\n')
            (temp / "leaves.cpp").write_text(stubs, encoding="utf-8")
            for enabled in (0, 1):
                exe = temp / f"screen-power-{enabled}.exe"
                run_native([shutil.which("g++"), "-std=c++17", "-DST7789_WIDTH=320u", "-DST7789_HEIGHT=172u", f"-DHBOX_AUTO_SLEEP_ENABLED={enabled}", "-I" + str(temp),
                            *application_include_flags(), "-I" + str(ROOT / "common"), "-I" + str(ROOT / "application/Libs/cJSON"),
                            "-I" + str(ROOT / "application/Inc/display/drivers/st7789"),
                            str(ROOT / "tools/tests/screen_power_config_test.cpp"), str(temp / "leaves.cpp"),
                            str(ROOT / "application/Src/display/screen_control/spi_screen_detail_pages.cpp"),
                            str(ROOT / "application/Src/display/screen_control/spi_screen_main_list.cpp"),
                            *[str(ROOT / "application/Src/display/screen_control" / ("spi_screen_detail_" + name + ".cpp")) for name in real_leaves.values()],
                            str(ROOT / "application/Src/display/screen_control/spi_screen_ui_common.cpp"),
                            str(ROOT / "application/Src/display/drivers/st7789/screen_text.c"),
                            str(ROOT / "application/Src/display/screen_control/spi_screen_timed_popup.cpp"),
                            str(ROOT / "application/Libs/cJSON/cJSON.c"), "-o", str(exe)], check=True)
                run_native([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
