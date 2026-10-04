import pathlib, shutil, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class FastLinkTests(unittest.TestCase):
    def test_tx_bridge_handshake_and_backpressure(self):
        cc=shutil.which('gcc')
        self.assertIsNotNone(cc)
        with tempfile.TemporaryDirectory(prefix='xora-bridge-') as folder:
            exe=pathlib.Path(folder)/'bridge.exe'
            subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'tools/tests/tx_image_bulk_stubs'),'-I'+str(ROOT/'common'),'-I'+str(ROOT/'RF_PHY_Hop/TX/USB'),
                str(ROOT/'tools/tests/webhid_fast_bridge_test.c'),
                str(ROOT/'RF_PHY_Hop/TX/USB/usb_webhid_fast.c'),
                str(ROOT/'RF_PHY_Hop/TX/USB/usb_webhid_memory.c'),'-o',str(exe)],check=True,timeout=120)
            subprocess.run([str(exe)],check=True,timeout=10)

    def test_wire_state_machine(self):
        cc=shutil.which('gcc')
        self.assertIsNotNone(cc, 'host C compiler is required')
        with tempfile.TemporaryDirectory(prefix='xora-fast-link-') as folder:
            exe=pathlib.Path(folder)/'fast-link.exe'
            subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'common'),str(ROOT/'tools/tests/webhid_fast_link_test.c'),'-o',str(exe)],
                check=True,timeout=120)
            subprocess.run([str(exe)],check=True,timeout=10)

    def test_usb_descriptor_and_report_layout(self):
        cc=shutil.which('gcc')
        self.assertIsNotNone(cc)
        with tempfile.TemporaryDirectory(prefix='xora-descriptor-') as folder:
            exe=pathlib.Path(folder)/'descriptor.exe'
            subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'common'),'-I'+str(ROOT/'RF_PHY_Hop/TX/USB'),
                str(ROOT/'tools/tests/usb_webhid_protocol_test.c'),
                str(ROOT/'RF_PHY_Hop/TX/USB/usb_webhid.c'),'-o',str(exe)],check=True,timeout=120)
            subprocess.run([str(exe)],check=True,timeout=10)
