import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

try:
    from .application_paths import application_include_flags, run_native
except ImportError:
    from application_paths import application_include_flags, run_native


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
                *application_include_flags(),
                "-I",
                str(ROOT / "application" / "Libs" / "sha256_simple"),
                "-I",
                str(ROOT / "common"),
                str(TESTS / "firmware_manager_reliability_test.cpp"),
                str(
                    ROOT / 'application/Src/firmware/firmware_manager.cpp'
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
            compile_result = run_native(
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
            run_result = run_native(
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
