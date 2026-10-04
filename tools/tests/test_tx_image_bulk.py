"""SPI-only native tests; no device access, RF regression or sampling."""
import pathlib, shutil, tempfile, unittest
from .application_paths import application_include_flags, run_native
ROOT=pathlib.Path(__file__).resolve().parents[2]
class TxImageBulkTests(unittest.TestCase):
    def test_production_tx_capability_and_flash_bounds(self):
        compiler=shutil.which('gcc')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='xora-tx-bulk-source-') as directory:
            exe=pathlib.Path(directory)/'source.exe'
            command=[compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Icommon','-IRF_PHY_Hop/TX/USB',
                     'tools/tests/tx_image_bulk_tx_control_test.c','-o',str(exe)]
            r=run_native(command,cwd=ROOT,capture_output=True,text=True,timeout=120)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=run_native([str(exe)],cwd=ROOT,capture_output=True,text=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_production_controller_consumer(self):
        compiler=shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='xora-tx-bulk-') as directory:
            exe=pathlib.Path(directory)/'controller.exe'
            command=[compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',
                     '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                     '-Itools/tests/tx_image_bulk_stubs','-Itools/tests/stubs','-Icommon',
                     *application_include_flags(),'tools/tests/tx_image_bulk_controller_test.cpp',
                     'common/usb_board_link_codec.c','-o',str(exe)]
            r=run_native(command,cwd=ROOT,capture_output=True,text=True,timeout=120)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=run_native([str(exe)],cwd=ROOT,capture_output=True,text=True,timeout=30)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('compatibility tests passed',r.stdout)
