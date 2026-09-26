from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

try:
    from .application_paths import application_include_flags, run_native
except ImportError:
    from application_paths import application_include_flags, run_native


ROOT = Path(__file__).resolve().parents[2]


class LatencyCycleContractTests(unittest.TestCase):
    def test_dwt_wrap_and_stage_sum(self) -> None:
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "a host C++ compiler is required")
        include = ROOT / 'application/Inc'
        source = ROOT / "tools" / "tests" / "cycle_elapsed_test.cpp"
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "cycle_elapsed_test.exe"
            compiled = run_native(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    *application_include_flags(),
                    str(source),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            ran = run_native([str(executable)], capture_output=True,
                                 text=True, check=False)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_telemetry_total_is_explicit_stage_sum(self) -> None:
        source = (
            ROOT / 'application/Src/diagnostics/monitor_telemetry.cpp'
        ).read_text(encoding="utf-8")
        self.assertIn("g_snapshot.latestAdcConversionUs +", source)
        self.assertIn("g_snapshot.latestInputProcessingUs +", source)
        self.assertIn("g_snapshot.latestReportSubmitUs", source)
        self.assertIn("latestSampleToCh585SubmitUs", source)
        self.assertIn("latestSampleToRfSubmitUs", source)
        self.assertNotIn("latestUsbLatencyUs", source)
        self.assertNotIn("now_us - t0_us", source)


if __name__ == "__main__":
    unittest.main()
