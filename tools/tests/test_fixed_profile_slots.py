import re
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


class FixedProfileSlotsTests(unittest.TestCase):
    def test_legacy_slot_migration_and_idempotence(self):
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='hbox-fixed-slots-') as temp:
            executable = Path(temp) / 'slots.exe'
            run_native([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            *application_include_flags(),
                            str(ROOT / 'tools/tests/fixed_profile_slots_test.cpp'),
                            '-o', str(executable)], check=True, capture_output=True, text=True)
            run_native([str(executable)], check=True, capture_output=True, text=True)

    def test_mock_capacity_matches_storage_reservation(self):
        firmware = (ROOT / 'application/Inc/system/board_cfg.h').read_text(encoding='utf-8')
        web = (ROOT / 'application/www/types/gamepad-config.ts').read_text(encoding='utf-8')
        count = int(re.search(r'#define\s+NUM_PROFILES\s+(\d+)', firmware)[1])
        self.assertEqual(count, int(re.search(r'NUM_PROFILES_MAX = (\d+)', web)[1]))


if __name__ == '__main__':
    unittest.main()
