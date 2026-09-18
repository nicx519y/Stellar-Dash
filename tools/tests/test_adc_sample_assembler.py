from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AdcSampleAssemblerTests(unittest.TestCase):
    def test_callback_order_missing_halves_and_latest_overwrite(self) -> None:
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "a host C++ compiler is required")
        source = ROOT / "tools" / "tests" / "adc_sample_assembler_test.cpp"
        include = ROOT / "application" / "Cpp_Core" / "Inc"
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "adc_sample_assembler_test.exe"
            compile_result = subprocess.run(
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
            self.assertEqual(
                compile_result.returncode,
                0,
                compile_result.stdout + compile_result.stderr,
            )
            run_result = subprocess.run(
                [str(executable)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )


if __name__ == "__main__":
    unittest.main()
