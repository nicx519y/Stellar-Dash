import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class UserImageQspiReliabilityTests(unittest.TestCase):
    def test_real_handler_uses_crc_and_header_last(self) -> None:
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "host C++ compiler is required")
        with tempfile.TemporaryDirectory(prefix="hbox-user-image-qspi-") as temp:
            executable = Path(temp) / "user-image-qspi-test.exe"
            command = [
                compiler,
                "-std=c++17",
                "-fpermissive",
                f"-I{ROOT / 'tools/tests/user_image_stubs'}",
                f"-I{ROOT / 'application/Cpp_Core/Inc'}",
                f"-I{ROOT / 'application/Libs/CRC32/src'}",
                str(
                    ROOT
                    / "application/Cpp_Core/Src/configs/user_image_command_handler.cpp"
                ),
                str(ROOT / "application/Libs/CRC32/src/CRC32.cpp"),
                str(ROOT / "tools/tests/user_image_command_handler_qspi_test.cpp"),
                "-o",
                str(executable),
            ]
            completed = subprocess.run(
                command,
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                completed.returncode,
                0,
                f"host compile failed:\n{completed.stdout}\n{completed.stderr}",
            )
            completed = subprocess.run(
                [str(executable)],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                completed.returncode,
                0,
                f"reliability test failed:\n{completed.stdout}\n{completed.stderr}",
            )
            self.assertIn(
                "user image QSPI reliability tests passed",
                completed.stdout,
            )


if __name__ == "__main__":
    unittest.main()
