import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tools" / "tests"


class FirmwareManagerReliabilityTests(unittest.TestCase):
    def test_production_firmware_manager_replay_and_commit_contract(self) -> None:
        compiler = shutil.which("g++")
        self.assertIsNotNone(
            compiler,
            "host g++ is required for the production FirmwareManager test",
        )
        with tempfile.TemporaryDirectory(prefix="hbox-firmware-manager-") as temp:
            executable = Path(temp) / "firmware_manager_reliability_test.exe"
            command = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-I",
                str(TESTS / "firmware_manager_stubs"),
                "-I",
                str(ROOT / "application" / "Cpp_Core" / "Inc"),
                "-I",
                str(ROOT / "application" / "Libs" / "sha256_simple"),
                "-I",
                str(ROOT / "common"),
                str(TESTS / "firmware_manager_reliability_test.cpp"),
                str(
                    ROOT
                    / "application"
                    / "Cpp_Core"
                    / "Src"
                    / "firmware"
                    / "firmware_manager.cpp"
                ),
                str(
                    ROOT
                    / "application"
                    / "Libs"
                    / "sha256_simple"
                    / "sha256_simple.c"
                ),
                "-o",
                str(executable),
            ]
            compile_result = subprocess.run(
                command,
                cwd=ROOT,
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
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                run_result.stdout + run_result.stderr,
            )
            self.assertIn(
                "firmware manager reliability tests passed",
                run_result.stdout,
            )


if __name__ == "__main__":
    unittest.main()
