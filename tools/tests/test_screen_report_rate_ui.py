"""Exercise screen selection and shared slider drawing without RF or hardware."""

from pathlib import Path
import shutil
import tempfile
import unittest

from .application_paths import ROOT, run_native


class ScreenReportRateUiTests(unittest.TestCase):
    def test_rate_selection_and_shared_slider(self):
        with tempfile.TemporaryDirectory(prefix="xora-screen-rate-") as directory:
            temp = Path(directory)
            (temp / "screen_control").mkdir()
            for name in ("spi_screen_layout.hpp", "spi_screen_detail_entries.hpp",
                         "spi_screen_detail_render_helpers.hpp", "spi_screen_ui_common.hpp"):
                shutil.copyfile(ROOT / "application/Inc/display/screen_control" / name,
                                temp / "screen_control" / name)
            shutil.copyfile(ROOT / "application/Inc/support/enums.hpp", temp / "enums.hpp")
            (temp / "st7789.h").write_text('''
#pragma once
#include <stdint.h>
#define ST7789_WIDTH 320u
#define ST7789_HEIGHT 172u
typedef struct { int unused; } ST7789_Handle;
void ST7789_FillRect(ST7789_Handle*, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t);
void ST7789_DrawString(ST7789_Handle*, uint16_t, uint16_t, const char*, uint32_t, uint32_t, uint8_t);
''', encoding="utf-8")
            (temp / "storagemanager.hpp").write_text('''
#pragma once
#include "enums.hpp"
struct StorageStub {
    WirelessReportRate rate = RFM_RATE_1K;
    WirelessReportRate getWirelessReportRate() { return rate; }
    void setWirelessReportRate(WirelessReportRate value) { rate = value; }
};
extern StorageStub STORAGE_MANAGER;
''', encoding="utf-8")
            # RF confirmation is outside this UI test; abort if accidentally used.
            (temp / "board_mode.hpp").write_text('''
#pragma once
#include <cstdlib>
enum class BoardMode { Usb, Rf };
struct BoardStub {
    bool isStable() { std::abort(); }
    BoardMode current() { std::abort(); }
};
extern BoardStub BOARD_MODE;
''', encoding="utf-8")
            (temp / "connection_manager.hpp").write_text('''
#pragma once
#include <cstdlib>
#include "enums.hpp"
struct ConnectionStub {
    ConnectionMode getMode() { std::abort(); }
    bool applyWirelessReportRate(WirelessReportRate, bool) { std::abort(); }
};
extern ConnectionStub CONNECTION_MANAGER;
''', encoding="utf-8")
            (temp / "board_cfg.h").write_text("#pragma once\n", encoding="utf-8")
            (temp / "system_logger.h").write_text(
                "#pragma once\n#define APP_DBG(...)\n#define APP_ERR(...)\n", encoding="utf-8")
            exe = temp / "screen-rate-ui.exe"
            compiler = shutil.which("g++")
            self.assertIsNotNone(compiler)
            run_native([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(temp), str(ROOT / "tools/tests/screen_report_rate_ui_test.cpp"),
                        *[str(ROOT / "application/Src/display/screen_control" / name)
                          for name in ("spi_screen_detail_tournament_mode.cpp",
                                       "spi_screen_detail_render_helpers.cpp", "spi_screen_ui_common.cpp")],
                        "-o", str(exe)], check=True)
            run_native([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
