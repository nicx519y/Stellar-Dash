import shutil
import tempfile
import unittest
from pathlib import Path
from .application_paths import ROOT, run_native

class JpegPlayerTests(unittest.TestCase):
    def test_production_player_handles_rows_completion_errors_and_timeout(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="xora-jpeg-player-") as directory:
            executable = str(Path(directory) / "player.exe")
            command = [compiler, "-std=c++17", "-fpermissive",
                       "-Itools/tests/jpeg_player_stubs", "-Iapplication/Inc/display",
                       "application/Src/display/screen_control/jpeg_player.cpp",
                       "tools/tests/jpeg_player_test.cpp", "-o", executable]
            result = run_native(command, cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = run_native([executable], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("JPEG playback state tests passed", result.stdout)
