import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class RfBindingTests(unittest.TestCase):
    def test_sdk_config_keeps_ble_storage_out_of_binding_banks(self):
        compiler = shutil.which('gcc')
        if not compiler:
            self.skipTest('host gcc unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            vendor = pathlib.Path(tmp)
            # Model the SDK's opt-out default and prove our include wrapper
            # still loads SDK definitions while disabling its Flash owner.
            (vendor / 'CONFIG.h').write_text(
                '#ifndef BLE_SNV\n#define BLE_SNV 1\n#endif\n'
                '#define SDK_CONFIG_LOADED 1\n', encoding='utf-8')
            (vendor / 'HAL.h').write_text('#include "CONFIG.h"\n', encoding='utf-8')
            source = vendor / 'probe.c'
            command = [compiler, '-E', '-I', str(ROOT / 'RF_PHY_Hop/Common/include'),
                       '-I', str(vendor), str(source)]
            for header in ('CONFIG.h', 'HAL.h'):
                source.write_text(
                    f'#include <{header}>\n'
                    '#if BLE_SNV || !SDK_CONFIG_LOADED\n#error bad SDK config\n#endif\n',
                    encoding='utf-8')
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run(command + ['-DBLE_SNV=1'], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('BLE SNV must remain disabled', result.stderr)

    def test_usb_maintenance_gate(self):
        compiler = shutil.which('gcc')
        if not compiler:
            self.skipTest('host gcc unavailable')
        usb = ROOT / 'RF_PHY_Hop/TX/USB'
        with tempfile.TemporaryDirectory() as tmp:
            exe = pathlib.Path(tmp) / 'management.exe'
            result = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'common'), '-I', str(usb),
                str(ROOT / 'tools/tests/usb_ncm_management_test.c'),
                str(usb / 'usb_ncm.c'), str(usb / 'usb_management_control.c'), '-o', str(exe)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_store_fault_injection(self):
        compiler = shutil.which('gcc')
        if not compiler:
            self.skipTest('host gcc unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            exe = pathlib.Path(tmp) / 'binding.exe'
            result = subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-Wno-type-limits', '-Wno-misleading-indentation',
                '-I', str(ROOT / 'RF_PHY_Hop/Common/include'),
                str(ROOT / 'tools/tests/rf_binding_store_test.c'), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('rf binding store tests passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
