"""Shared descriptor headers keep legacy C/C++ includes byte-for-byte compatible."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class SharedUsbDescriptorsTests(unittest.TestCase):
    def test_old_and_public_headers_preprocess_identically_for_c_and_cpp(self):
        for driver, name in [("xinput", "XInputDescriptors.hpp"), ("switch", "SwitchDescriptors.hpp")]:
            public = ROOT / "common/usb" / name
            legacy = ROOT / "application/Inc/transport/usb/legacy/drivers" / driver / name
            for compiler_name, language in [("gcc", "c"), ("g++", "c++")]:
                with self.subTest(header=name, language=language):
                    compiler = shutil.which(compiler_name)
                    self.assertIsNotNone(compiler)
                    outputs = []
                    with tempfile.TemporaryDirectory() as temporary:
                        source = pathlib.Path(temporary) / "descriptor.c"
                        for header in [legacy, public]:
                            source.write_text('#include "' + header.as_posix() + '"\n')
                            result = subprocess.run([compiler, "-x", language, "-E", "-P", str(source)],
                                capture_output=True, text=True, timeout=120)
                            self.assertEqual(result.returncode, 0, result.stderr)
                            outputs.append([line for line in result.stdout.splitlines() if line.strip()])
                    self.assertEqual(outputs[0], outputs[1])

    def test_tx_depends_on_public_descriptor_headers(self):
        consumers = [("usb_device_port_ch585.c", "XInputDescriptors.hpp"),
                     ("usb_legacy_descriptors.c", "SwitchDescriptors.hpp")]
        for source, header in consumers:
            text = (ROOT / "RF_PHY_Hop/TX/USB" / source).read_text()
            self.assertIn('#include "../../../common/usb/' + header + '"', text)
            self.assertNotIn('#include "../../../application/', text)
