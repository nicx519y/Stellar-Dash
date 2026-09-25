import os
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def run_checked(command):
    started = time.monotonic()
    process = subprocess.Popen(command, start_new_session=os.name != "nt")
    print(f"PID={process.pid}, timeout=120s", flush=True)
    try:
        code = process.wait(timeout=120)
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], timeout=15)
        else:
            os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=15)
        raise
    print(f"exit={code}, elapsed={time.monotonic() - started:.2f}s", flush=True)
    if code:
        raise subprocess.CalledProcessError(code, command)


class LedDmaRuntimeTests(unittest.TestCase):
    def test_production_driver_with_dma_and_timer_stubs(self):
        self.run_variant(False)

    def test_diagnostic_driver_with_dma_and_timer_stubs(self):
        self.run_variant(True)

    def test_hold_diagnostic_with_dma_and_timer_stubs(self):
        self.run_variant(2)

    def run_variant(self, diagnostic):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "host g++ is required")
        with tempfile.TemporaryDirectory(prefix="hbox-led-dma-") as directory:
            temp = Path(directory)
            for header in ("tim.h", "utils.h", "board_cfg.h", "board_power.hpp"):
                (temp / header).write_text('#pragma once\n#include "led_dma_runtime_stubs.hpp"\n', encoding="utf-8")
            executable = temp / "led_dma_runtime.exe"
            print("LED DMA: compiling actual driver with fake HAL", flush=True)
            run_checked([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                         f"-DHBOX_LED_DMA_DIAGNOSTIC={int(diagnostic)}",
                         "-I", str(temp), "-I", str(ROOT / "tools/tests"),
                         str(ROOT / "tools/tests/led_dma_runtime_test.cpp"),
                         "-o", str(executable)])
            scenarios = ("normal", "delayed", "dual", "error", "boundaries", "encoding", "dma-latency", "start-phase")
            if diagnostic:
                scenarios += ("diagnostic",)
            for scenario in scenarios:
                with self.subTest(scenario=scenario):
                    print(f"LED DMA: {scenario}", flush=True)
                    run_checked([str(executable), scenario])


if __name__ == "__main__":
    unittest.main()
