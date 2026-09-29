import pathlib
import shutil
import tempfile
import unittest
from .application_paths import run_native

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ReleaseJournalTests(unittest.TestCase):
    def test_production_journal_interrupted_writes(self):
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler, 'host g++ required')
        with tempfile.TemporaryDirectory(prefix='xora-release-journal-') as directory:
            output = pathlib.Path(directory) / 'journal.exe'
            command = [compiler, '-std=c++17', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                       '-Itools/tests/release_install_stubs', '-Iapplication/Inc/firmware', '-Icommon',
                       '-Iapplication/Libs/cJSON', '-Iapplication/Libs/sha256_simple',
                       'tools/tests/release_install_journal_test.cpp',
                       'application/Libs/cJSON/cJSON.c', 'application/Libs/sha256_simple/sha256_simple.c', '-o', str(output)]
            result = run_native(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = run_native([str(output)], cwd=ROOT, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('interruption tests passed', result.stdout)
