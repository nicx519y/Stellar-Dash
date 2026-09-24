import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class FixedProfileSlotsTests(unittest.TestCase):
    def test_legacy_slot_migration_and_idempotence(self):
        compiler = shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='hbox-fixed-slots-') as temp:
            executable = Path(temp) / 'slots.exe'
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'application/Cpp_Core/Inc'),
                            str(ROOT / 'tools/tests/fixed_profile_slots_test.cpp'),
                            '-o', str(executable)], check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], check=True, capture_output=True, text=True)

    def test_mock_capacity_matches_storage_reservation(self):
        firmware = (ROOT / 'application/Core/Inc/board_cfg.h').read_text(encoding='utf-8')
        web = (ROOT / 'application/www/types/gamepad-config.ts').read_text(encoding='utf-8')
        count = int(re.search(r'#define\s+NUM_PROFILES\s+(\d+)', firmware)[1])
        self.assertEqual(count, int(re.search(r'NUM_PROFILES_MAX = (\d+)', web)[1]))


if __name__ == '__main__':
    unittest.main()
