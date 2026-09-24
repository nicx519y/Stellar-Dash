"""Native tests of the production passive handoff wait; no device access."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tools/tests"
INC = ROOT / "application/Cpp_Core/Inc"


class UsbRoleReadyWaitTest(unittest.TestCase):
    def compile_run(self, folder, sources, includes):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        exe = folder / "test.exe"
        subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
             *[f"-I{path}" for path in includes], *map(str, sources),
             "-o", str(exe)], check=True, timeout=60, cwd=ROOT)
        subprocess.run([str(exe)], check=True, timeout=10, cwd=ROOT)

    def test_passive_wait_boundaries(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source = folder / "test.cpp"
            source.write_text(r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include "usb_role_ready_wait.hpp"
int main() {
    // Normal pulse, pulse already low, missed pulse, stuck low/high, unknown
    // ACK release, delayed pulse, tick wrap, and a short false high in pulse.
    for (uint32_t origin : {0u, 0xFFFFFFC0u}) {
        for (uint32_t step : {1u, 2u}) {
            for (unsigned scenario = 0; scenario < 9; ++scenario) {
                uint32_t now = origin;
                unsigned reads = 0;
                const bool early = UsbRoleReady::wait(scenario != 5,
                    [&] { return now; },
                    [&] {
                        ++reads;
                        uint32_t t = now - origin;
                        switch (scenario) {
                        case 0: return t < 4u || t >= 104u;
                        case 1: return t >= 80u;
                        case 2: case 4: return true;
                        case 3: return false;
                        case 5: return t >= 20u;
                        case 6: return t < 120u || t >= 220u;
                        case 7: return t < 4u || t >= 104u;
                        default: return t == 20u || t >= 100u;
                        }
                    },
                    [&](uint32_t ms) { now += ms + step - 1u; });
                const uint32_t elapsed = now - origin;
                if (scenario == 0 || scenario == 7) {
                    assert(early && elapsed >= 106u && elapsed <= 108u);
                } else if (scenario == 1) {
                    assert(early && elapsed >= 82u && elapsed <= 84u);
                } else if (scenario == 8) {
                    assert(early && elapsed >= 102u && elapsed <= 104u);
                } else {
                    assert(!early && elapsed >= 150u && elapsed <= 151u);
                }
                if (scenario == 5) assert(reads == 0u);
            }
        }
    }
}
''', encoding="utf-8")
            self.compile_run(folder, [source], [INC])

    def test_link_credit_resume_compatibility(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.compile_run(Path(temporary), [
                TESTS / "usb_board_link_tx_resume_test.cpp",
                ROOT / "application/Cpp_Core/Src/usb_board_link.cpp",
                ROOT / "common/usb_board_link_codec.c",
            ], [TESTS / "stubs", ROOT / "common", INC])


if __name__ == "__main__":
    unittest.main()
