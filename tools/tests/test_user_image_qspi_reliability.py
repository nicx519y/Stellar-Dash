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


class UserImageQspiReliabilityTests(unittest.TestCase):
    def test_real_handler_uses_crc_and_header_last(self) -> None:
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler, "host C++ compiler is required")
        with tempfile.TemporaryDirectory(prefix="hbox-user-image-qspi-") as temp:
            # Compile the production RPC validator with the real image handler;
            # a handler-only fixture misses responses rejected by the envelope.
            service = (ROOT / 'application/Src/webconfig/webhid_service.cpp').read_text(
                encoding="utf-8")
            firmware_header = (
                ROOT / 'application/Inc/webconfig/configs/firmware_command_handler.hpp'
            ).read_text(encoding="utf-8")
            (Path(temp) / "image_ack_validator.hpp").write_text(
                '#include "cJSON.h"\n'
                '#include "webhid_protocol.h"\n'
                + firmware_header[firmware_header.index("#define BINARY_CMD_UPLOAD_FIRMWARE_CHUNK"):
                                  firmware_header.index("/**")]
                + service[service.index("uint32_t loadLe32("):
                          service.index("bool constantTimeEqual(")]
                + service[service.index("enum class BinaryAckStatus"):
                          service.index("cJSON *createBinaryAckData")],
                encoding="utf-8")
            executable = Path(temp) / "user-image-qspi-test.exe"
            command = [
                compiler,
                "-std=c++17",
                "-fpermissive",
                f"-I{temp}",
                f"-I{ROOT / 'tools/tests/user_image_stubs'}",
                *application_include_flags(),
                f"-I{ROOT / 'application/Libs/CRC32/src'}",
                f"-I{ROOT / 'application/Libs/cJSON'}",
                f"-I{ROOT / 'common'}",
                str(
                    ROOT / 'application/Src/webconfig/configs/user_image_command_handler.cpp'
                ),
                str(ROOT / "application/Libs/CRC32/src/CRC32.cpp"),
                str(ROOT / "application/Libs/cJSON/cJSON.c"),
                str(ROOT / "tools/tests/user_image_command_handler_qspi_test.cpp"),
                "-o",
                str(executable),
            ]
            completed = run_native(
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
            completed = run_native(
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
