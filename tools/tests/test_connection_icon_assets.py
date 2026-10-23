"""Offline icon conversion/pack regression; never opens hardware."""
from pathlib import Path
import binascii
import shutil
import struct
import sys
import tempfile
import unittest

from PIL import Image
from tools.assets_fix import _convert_rgb888_to_rgb565le_bytes
from tools.pack_assets import build_pack
from tools.tests.application_paths import ROOT, run_native


class ConnectionIconAssetsTests(unittest.TestCase):
    def test_repeated_image_conversion_keeps_native_rgb565_and_pack_crc(self):
        with tempfile.TemporaryDirectory(prefix="xora-connection-icons-") as folder:
            temp = Path(folder)
            # These are the exact image and pre-generated raw combinations used
            # by build/release. Grey pixels expose byte swaps that white hides.
            names = [f"{kind}_{theme}" for kind in ("wireless", "USB") for theme in ("dark", "light")]
            expected = {}
            for name in names:
                png = ROOT / "application/assets/sysicons" / f"{name}.png"
                shutil.copyfile(png, temp / png.name)
                with Image.open(png) as image:
                    expected[name] = _convert_rgb888_to_rgb565le_bytes(image.convert("RGB").tobytes(), 28, 28)
                # Simulate the old big-endian output: source PNG must repair it.
                raw = expected[name]
                (temp / f"{name}_28x28.rgb565").write_bytes(b"".join(raw[i:i+2][::-1] for i in range(0, len(raw), 2)))
            orphan = temp / "raw_only_1x1.rgb565"
            orphan.write_bytes(b"\x10\x84")
            command = [sys.executable, str(ROOT / "tools/assets_fix.py"), "--dir", str(temp),
                       "--out", str(temp), "--fit", "320x170", "--inplace", "--dither"]
            for _ in range(2):
                run_native(command, check=True, timeout=30)
                for name, raw in expected.items():
                    self.assertEqual((temp / f"{name}_28x28.rgb565").read_bytes(), raw)
                self.assertEqual(orphan.read_bytes(), b"\x10\x84")
            output = temp / "system_assets.bin"
            build_pack(temp, output, 0x40000)
            data = output.read_bytes()
            magic, version, _, total, count, _, _ = struct.unpack_from("<4sHHIIII", data)
            self.assertEqual((magic, version, total, count), (b"HIMG", 1, len(data), 5))
            seen = set()
            for i in range(count):
                name, kind, _, _, offset, length, w, h, crc = struct.unpack_from("<32sBBHIIHHI", data, 64+i*64)
                name = name.rstrip(b"\0").decode()
                payload = data[offset:offset+length]
                self.assertEqual(binascii.crc32(payload) & 0xffffffff, crc)
                if name in expected:
                    self.assertEqual((kind, w, h, length), (2, 28, 28, 1568))
                    self.assertEqual(payload, expected[name])
                    seen.add(name)
            self.assertEqual(seen, set(names))
            # Explicit conversion remains available for known big-endian raw
            # imports, without swapping freshly generated PNG outputs.
            run_native(command + ["--swap-rgb565-bytes"], check=True, timeout=30)
            self.assertEqual(orphan.read_bytes(), b"\x84\x10")
            for name, raw in expected.items():
                self.assertEqual((temp / f"{name}_28x28.rgb565").read_bytes(), raw)


if __name__ == "__main__":
    unittest.main()
