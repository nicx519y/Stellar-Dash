"""Production backlight ramp: unchanged dark hold, three-second fade."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from tools.tests.test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class StartupBacklightTest(unittest.TestCase):
    def test_three_second_ramp_and_tick_wrap(self):
        source = (ROOT / 'application/Src/display/screen_control/spi_screen_manager.cpp').read_text(encoding='utf-8')
        definitions = '\n'.join(re.findall(r'^#define SPI_SCREEN_BL_(?:INIT_HOLD|RAMP)_MS .+$', source, re.M))
        harness = r'''
#include <cstdint>
#include <cassert>
bool g_brightnessPreviewActive=false, g_bl_ramp_active=true;
uint8_t g_previewBrightness=40, g_cfgBrightness=80;
uint32_t g_bl_boot_ms=100;
'''
        cases = r'''
int main() {
    assert(compute_backlight_percent(100)==0);
    assert(compute_backlight_percent(1099)==0);
    assert(compute_backlight_percent(1100)==0);
    assert(compute_backlight_percent(2600)==40);
    assert(compute_backlight_percent(4099)==79 && g_bl_ramp_active);
    assert(compute_backlight_percent(4100)==80 && !g_bl_ramp_active);
    g_brightnessPreviewActive=true;
    assert(compute_backlight_percent(4200)==40);
    g_bl_ramp_active=true; g_bl_boot_ms=UINT32_MAX-10;
    assert(compute_backlight_percent(g_bl_boot_ms+2500u)==20);
    assert(compute_backlight_percent(g_bl_boot_ms+4000u)==40 && !g_bl_ramp_active);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp, exe = Path(tmp) / 'backlight.cpp', Path(tmp) / 'backlight.exe'
            cpp.write_text(definitions + '\n' + harness + function(source, 'static uint8_t compute_backlight_percent(') + cases, encoding='utf-8')
            print('Compile/run backlight hold, 3s ramp, preview and tick wrap', flush=True)
            subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True, timeout=60)
            subprocess.run([str(exe)], check=True, timeout=5)
