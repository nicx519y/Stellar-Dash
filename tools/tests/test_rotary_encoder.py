import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class RotaryEncoderTests(unittest.TestCase):
    def test_fixed_tick_decoder_and_button_debounce(self) -> None:
        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler, "host gcc is required for rotary tests")

        with tempfile.TemporaryDirectory(prefix="hbox-rotary-") as temp:
            executable = Path(temp) / "rotary_encoder_test.exe"
            result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DROTENC_DEBUG_PRINT=0",
                    "-I",
                    str(ROOT / "tools" / "tests" / "rotary_stubs"),
                    "-I",
                    str(ROOT / "application" / "Drivers" / "ROTARY-ENCODER"),
                    str(ROOT / "tools" / "tests" / "rotary_encoder_test.c"),
                    str(
                        ROOT
                        / "application"
                        / "Drivers"
                        / "ROTARY-ENCODER"
                        / "rotary-encoder.c"
                    ),
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            run = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("rotary encoder tests passed", run.stdout)

    def test_runtime_wiring_and_ui_consumption_are_bounded(self) -> None:
        interrupts = (
            ROOT / "application" / "Core" / "Src" / "stm32h7xx_it.c"
        ).read_text(encoding="utf-8")
        screen = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "screen_control"
            / "spi_screen_manager.cpp"
        ).read_text(encoding="utf-8")
        input_state = (
            ROOT
            / "application"
            / "Cpp_Core"
            / "Src"
            / "states"
            / "input_state.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("RotEnc_Tick1msFromISR();", interrupts)
        self.assertIn("while (det > 0)", screen)
        self.assertIn("while (det < 0)", screen)
        self.assertIn("kUsbReportCatchupLimit", input_state)
        self.assertNotIn(
            "while (REPORT_SCHEDULER.consumeTick())", input_state
        )


if __name__ == "__main__":
    unittest.main()
