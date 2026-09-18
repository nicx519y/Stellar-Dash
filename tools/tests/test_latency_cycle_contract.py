from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class LatencyCycleContractTests(unittest.TestCase):
    def test_dwt_wrap_and_stage_sum(self) -> None:
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "a host C++ compiler is required")
        include = ROOT / "application" / "Cpp_Core" / "Inc"
        source = ROOT / "tools" / "tests" / "cycle_elapsed_test.cpp"
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "cycle_elapsed_test.exe"
            compiled = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{include}",
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
            ran = subprocess.run([str(executable)], capture_output=True,
                                 text=True, check=False)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_telemetry_total_is_explicit_stage_sum(self) -> None:
        source = (
            ROOT / "application" / "Cpp_Core" / "Src" /
            "monitor_telemetry.cpp"
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
